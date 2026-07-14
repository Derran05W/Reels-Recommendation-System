// concurrency_check — Phase 11 task 1 (D13): verify that concurrent const HNSWVectorIndex::search()
// on a fully-built, frozen index is data-race-free. This is the first task of Phase 11: the load
// driver (benchmark_recommender) shares ONE read-only HNSW index across T client threads, so the
// design is only sound if concurrent const search touches no shared mutable state.
//
// This probe links only rr_vindex (D2 containment: no vector-db header here, no orchestrator/
// candidate-source symbols — those live in rr_recommend). It therefore verifies the ONE genuinely
// shared object in the load driver: the HNSWVectorIndex. The per-thread pipeline components
// (candidate sources, ranker, reranker, orchestrator, exploration Rng) are NOT shared between
// threads by construction, so the full-pipeline concurrency is verified separately by building and
// running benchmark_recommender --smoke in the same ThreadSanitizer tree (see
// results/published/phase11/concurrency/VERDICT.md).
//
// Two independent evidences, both under one process so TSan instruments them together:
//   Phase A (distinct streams): T threads each issue M const searches with their OWN query stream.
//     A hidden shared write on the search path (mutable scratch, lazy cache, refcount churn) would
//     surface as a TSan data-race report; a plain-Release run would surface as malformed results.
//   Phase B (determinism cross-check): a set of SHARED identical queries is answered
//   single-threaded
//     BEFORE the threads start (the reference), then every thread re-answers those same queries
//     concurrently. Concurrent const search must return byte-identical id lists to the reference —
//     any divergence means the search read corrupted shared state.
//
// Exit code 0 with no TSan report is the pass signal. Excluded from ctest like all perf/concurrency
// executables (D7).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"
#include "rr/infrastructure/clock.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/vector_index.hpp"
#include "rr/vindex/hnsw_vector_index.hpp"

namespace {

// Generate `count` L2-normalized `dim`-d embeddings (gaussian-per-component direction). D8: only
// rr::Rng.gaussian(), never std::normal_distribution.
std::vector<rr::Embedding> generateEmbeddings(rr::Rng &rng, std::size_t count, std::size_t dim) {
    std::vector<rr::Embedding> data;
    data.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        rr::Embedding e(dim);
        for (std::size_t d = 0; d < dim; ++d) {
            e[d] = static_cast<float>(rng.gaussian());
        }
        rr::normalize(e);
        data.push_back(std::move(e));
    }
    return data;
}

// Per-thread findings. Each thread writes ONLY its own slot (distinct memory, no cross-thread
// aliasing), so this vector needs no synchronization.
struct ThreadResult {
    std::uint64_t searches = 0;
    std::uint64_t malformed = 0;  // results not well-formed (size/order/range/finite)
    std::uint64_t mismatches = 0; // determinism-query results != single-threaded reference
    std::uint64_t idChecksum = 0; // consumes results so nothing is optimized away
};

// Well-formedness of one const search result at query k.
bool wellFormed(const std::vector<rr::VectorSearchResult> &res, std::size_t k, std::size_t n) {
    if (res.size() > k || res.size() > n) {
        return false;
    }
    float prev = -1.0f;
    for (const rr::VectorSearchResult &r : res) {
        if (r.reelId.value >= n) {
            return false;
        }
        if (!(r.distance >= prev - 1e-4f)) { // ascending distance (float slack)
            return false;
        }
        prev = r.distance;
        // similarity = 1 - d^2/2 for unit vectors; stays in a sane band.
        if (!(r.similarity <= 1.01f && r.similarity >= -1.01f)) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint32_t> idsOf(const std::vector<rr::VectorSearchResult> &res) {
    std::vector<std::uint32_t> ids;
    ids.reserve(res.size());
    for (const rr::VectorSearchResult &r : res) {
        ids.push_back(r.reelId.value);
    }
    return ids;
}

} // namespace

int main(int argc, char **argv) {
    // ---- args -----------------------------------------------------------------------------------
    std::uint64_t seed = 42;
    std::size_t vectors = 20000;
    std::size_t dim = 64;
    std::size_t threads = 8;
    std::size_t searches = 2000; // const searches per thread
    std::size_t k = 500;
    std::size_t sharedQueries = 64; // determinism cross-check query set
    bool smoke = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << what << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--seed") {
            seed = std::stoull(next("--seed"));
        } else if (a == "--vectors") {
            vectors = std::stoull(next("--vectors"));
        } else if (a == "--dim") {
            dim = std::stoull(next("--dim"));
        } else if (a == "--threads") {
            threads = std::stoull(next("--threads"));
        } else if (a == "--searches") {
            searches = std::stoull(next("--searches"));
        } else if (a == "--k") {
            k = std::stoull(next("--k"));
        } else if (a == "--smoke") {
            smoke = true;
        } else if (a == "--help" || a == "-h") {
            std::cout
                << "usage: concurrency_check [--seed N] [--vectors N] [--dim D] [--threads N] "
                   "[--searches N] [--k N] [--smoke]\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            return 2;
        }
    }
    if (smoke) {
        vectors = 3000;
        threads = 4;
        searches = 300;
        k = 50;
        sharedQueries = 32;
    }
    if (threads == 0) {
        threads = 1;
    }

    std::cerr << "concurrency_check: vectors=" << vectors << " dim=" << dim
              << " threads=" << threads << " searches/thread=" << searches << " k=" << k
              << (smoke ? "  [SMOKE]\n" : "\n");

    // ---- build + FREEZE the shared index --------------------------------------------------------
    rr::HNSWConfig cfg; // m=16, ef_construction=200, ef_search=64 (rr defaults)
    const std::uint64_t hnswSeed = rr::splitmix64(seed ^ rr::fnv1a64("concurrency-index"));
    rr::HNSWVectorIndex index(dim, cfg, hnswSeed);
    {
        rr::Rng dataRng = rr::forkRng(seed, "concurrency-data");
        const std::vector<rr::Embedding> data = generateEmbeddings(dataRng, vectors, dim);
        rr::Stopwatch build;
        for (std::size_t i = 0; i < data.size(); ++i) {
            index.insert(rr::ReelId{static_cast<std::uint32_t>(i)}, data[i]);
        }
        std::cerr << "concurrency_check: built index (" << index.size() << " vectors) in "
                  << build.elapsedMs()
                  << " ms; index is now frozen (no further insert/setEfSearch)\n";
    }
    // From here on the index is READ-ONLY. setEfSearch mutates ef_search_ and MUST NOT be called.

    // ---- determinism reference (single-threaded, before any thread starts) ----------------------
    rr::Rng sharedQueryRng = rr::forkRng(seed, "concurrency-shared-queries");
    const std::vector<rr::Embedding> sharedQ =
        generateEmbeddings(sharedQueryRng, sharedQueries, dim);
    std::vector<std::vector<std::uint32_t>> reference(sharedQ.size());
    for (std::size_t q = 0; q < sharedQ.size(); ++q) {
        reference[q] = idsOf(index.search(sharedQ[q], k));
    }

    // ---- concurrent phase -----------------------------------------------------------------------
    std::vector<ThreadResult> results(threads);
    std::atomic<bool> go{false};

    auto worker = [&](std::size_t t) {
        // Distinct query stream per thread (D8): different threads never issue the same random
        // query, so Phase A genuinely exercises independent concurrent searches.
        rr::Rng rng = rr::forkRng(seed, "concurrency-thread-" + std::to_string(t));
        ThreadResult &out = results[t];
        go.wait(false); // block until all threads are spawned; maximizes real overlap

        for (std::size_t i = 0; i < searches; ++i) {
            if (!sharedQ.empty() && (i % 4 == 0)) {
                // Phase B: a shared determinism query. Must match the single-threaded reference.
                const std::size_t q = i % sharedQ.size();
                const auto res = index.search(sharedQ[q], k);
                if (!wellFormed(res, k, vectors)) {
                    ++out.malformed;
                }
                if (idsOf(res) != reference[q]) {
                    ++out.mismatches;
                }
                if (!res.empty()) {
                    out.idChecksum += res.front().reelId.value;
                }
            } else {
                // Phase A: a distinct concurrent const search.
                rr::Embedding query(dim);
                for (std::size_t d = 0; d < dim; ++d) {
                    query[d] = static_cast<float>(rng.gaussian());
                }
                rr::normalize(query);
                const auto res = index.search(query, k);
                if (!wellFormed(res, k, vectors)) {
                    ++out.malformed;
                }
                if (!res.empty()) {
                    out.idChecksum += res.front().reelId.value;
                }
            }
            ++out.searches;
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(threads);
    rr::Stopwatch wall;
    for (std::size_t t = 0; t < threads; ++t) {
        pool.emplace_back(worker, t);
    }
    go.store(true);
    go.notify_all();
    for (std::thread &th : pool) {
        th.join();
    }
    const double wallMs = wall.elapsedMs();

    // ---- aggregate + verdict --------------------------------------------------------------------
    std::uint64_t totalSearches = 0, totalMalformed = 0, totalMismatches = 0, checksum = 0;
    for (const ThreadResult &r : results) {
        totalSearches += r.searches;
        totalMalformed += r.malformed;
        totalMismatches += r.mismatches;
        checksum += r.idChecksum;
    }

    std::cerr << "concurrency_check: " << totalSearches << " concurrent const searches across "
              << threads << " threads in " << wallMs << " ms\n"
              << "  malformed results     : " << totalMalformed << "\n"
              << "  determinism mismatches: " << totalMismatches << "\n"
              << "  id checksum           : " << checksum << " (anti-DCE)\n";

    const bool pass = (totalMalformed == 0 && totalMismatches == 0);
    if (!pass) {
        std::cerr << "concurrency_check: FAIL — concurrent const search produced malformed or "
                     "divergent results; the frozen index is NOT safe for concurrent readers on "
                     "this toolchain. Fall back to per-thread index replicas (D13).\n";
        return 1;
    }
    std::cerr << "concurrency_check: PASS — concurrent const search on the frozen index is "
                 "well-formed and deterministic. Under -fsanitize=thread a clean exit (no race "
                 "report) is the stronger proof of data-race freedom.\n";
    std::cout << "CONCURRENCY_CHECK=PASS searches=" << totalSearches << " threads=" << threads
              << "\n";
    return 0;
}
