// benchmark_retrieval — standalone HNSW-vs-exact retrieval benchmark (ReelRank Phase 1, task 4).
//
// Sweeps a deliberate SUBSET of the TDD 17.3 grid (see plan/01-PHASES-FOUNDATION.md Phase 1):
//   vector counts : {10000, 100000}
//   dimensions    : 64            (fixed)
//   M             : {8, 16, 32}
//   efConstruction: 200           (fixed at the rr::HNSWConfig default; NOT swept over
//                                   {50,100,200,400}, to keep wall-clock sane — documented in
//                                   metadata.json "sweep.subset_note")
//   efSearch      : {16, 32, 64, 128, 256}
//   k             : {10, 50, 200, 500}
//
// For each (vectorCount, M): build ONE HNSWVectorIndex (measure build time / insert throughput /
// RSS delta / graph level distribution once), then sweep efSearch x k on that same built graph via
// setEfSearch() — the graph is never rebuilt per efSearch. Exact ground truth is one
// ExactVectorIndex per vectorCount; each query's exact top-500 is computed once and sliced for
// Recall@{10,50,200,500}.
//
// Design-decision compliance:
//   D2  — this file lives under apps/ (NOT src/vindex/), so it includes NO vector-db header;
//         everything is reached through rr::HNSWVectorIndex / rr::ExactVectorIndex / rr::*.
//   D7  — performance benchmark, separate apps/ executable, excluded from ctest.
//   D8  — all randomness via rr::Rng / forkRng; no std::*_distribution.
//   D9  — wall-clock only inside rr::Stopwatch for latency; nothing else reads a real clock except
//         the experiment-id timestamp and metadata generation.
//   D12 — custom lightweight harness (warmup + timed samples, p50/p95/p99 from stored samples);
//         writes results/<experiment-id>/{config.json,summary.json,*.csv,metadata.json}.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"
#include "rr/infrastructure/clock.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/vindex/exact_vector_index.hpp"
#include "rr/vindex/hnsw_vector_index.hpp"

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#elif defined(__linux__)
#include <sys/utsname.h>
#include <unistd.h>
#endif

// Repo/build facts injected by CMake (see apps/CMakeLists.txt).
#ifndef RR_REPO_DIR
#define RR_REPO_DIR "unknown"
#endif
#ifndef RR_VDB_DIR
#define RR_VDB_DIR "unknown"
#endif
#ifndef RR_BUILD_TYPE
#define RR_BUILD_TYPE "unknown"
#endif
#ifndef RR_COMPILER
#define RR_COMPILER "unknown"
#endif

namespace {

// ---------------------------------------------------------------------------------------------
// Harness helpers (D12). percentile()/currentRssBytes() are the portable prior-art snippets from
// vector-db's bench/mem_stats.hpp, reused verbatim (they have zero vector-db-specific deps, so
// copying them here does NOT violate D2 containment — no vector-db header is included).
// ---------------------------------------------------------------------------------------------

double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double rank = (p / 100.0) * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(rank));
    const size_t hi = static_cast<size_t>(std::ceil(rank));
    if (lo == hi) return values[lo];
    const double frac = rank - static_cast<double>(lo);
    return values[lo] * (1.0 - frac) + values[hi] * frac;
}

// Resident set size of the current process, in bytes; 0 when unavailable. Process-wide, so
// per-index deltas are approximate — record before/after pairs and report the delta.
size_t currentRssBytes() {
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<size_t>(info.resident_size);
    }
    return 0;
#elif defined(__linux__)
    long rss_pages = 0;
    if (FILE *f = std::fopen("/proc/self/statm", "r")) {
        long size_pages = 0;
        if (std::fscanf(f, "%ld %ld", &size_pages, &rss_pages) != 2) rss_pages = 0;
        std::fclose(f);
    }
    return rss_pages > 0 ? static_cast<size_t>(rss_pages) * static_cast<size_t>(sysconf(_SC_PAGESIZE))
                         : 0;
#else
    return 0;
#endif
}

// ---------------------------------------------------------------------------------------------
// Small utilities.
// ---------------------------------------------------------------------------------------------

// Run a shell command, return trimmed stdout ("" on failure). Used only for git provenance in
// metadata.json — never on any measured hot path.
std::string runCommand(const std::string &cmd) {
    std::string out;
    if (FILE *pipe = ::popen(cmd.c_str(), "r")) {
        char buf[512];
        while (std::fgets(buf, sizeof(buf), pipe) != nullptr) out += buf;
        ::pclose(pipe);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

std::string jsonEscape(const std::string &s) {
    std::string r;
    r.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\t': r += "\\t"; break;
        default: r += c; break;
        }
    }
    return r;
}

std::string nowStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return oss.str();
}

std::string nowIso() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

#if defined(__APPLE__)
std::string sysctlString(const char *name) {
    size_t len = 0;
    if (sysctlbyname(name, nullptr, &len, nullptr, 0) != 0 || len == 0) return "";
    std::string buf(len, '\0');
    if (sysctlbyname(name, buf.data(), &len, nullptr, 0) != 0) return "";
    if (!buf.empty() && buf.back() == '\0') buf.pop_back();
    return buf;
}
uint64_t sysctlU64(const char *name) {
    uint64_t v = 0;
    size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, nullptr, 0) != 0) return 0;
    return v;
}
#endif

// Generate `count` L2-normalized `dim`-d embeddings with isotropic (gaussian-per-component)
// directions. D8: rr::Rng.gaussian() only — no std::normal_distribution.
std::vector<rr::Embedding> generateEmbeddings(rr::Rng &rng, size_t count, size_t dim) {
    std::vector<rr::Embedding> data;
    data.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        rr::Embedding e(dim);
        for (size_t d = 0; d < dim; ++d) e[d] = static_cast<float>(rng.gaussian());
        rr::normalize(e);
        data.push_back(std::move(e));
    }
    return data;
}

// ---------------------------------------------------------------------------------------------
// Result row (one per (vectorCount, M, efSearch, k)).
// ---------------------------------------------------------------------------------------------
struct Row {
    size_t vectorCount;
    size_t dimensions;
    uint32_t m;
    uint32_t efConstruction;
    size_t efSearch;
    size_t k;
    double recallAtK;
    double avgReturned; // mean number of results the ANN actually returned (<= k)
    double p50Ms;
    double p95Ms;
    double p99Ms;
    double meanMs;
    size_t numQueries;
    // per-(vectorCount,M) build facts, repeated across the efSearch x k rows:
    double buildTimeMs;
    double insertThroughput;
    long long memoryDeltaBytes;
    size_t maxGraphLevel;
    size_t numLevels;
};

struct GraphLevelRow {
    size_t vectorCount;
    uint32_t m;
    uint32_t efConstruction;
    size_t maxGraphLevel;
    std::vector<size_t> levelDistribution;
    double buildTimeMs;
    double insertThroughput;
    long long memoryDeltaBytes;
};

} // namespace

int main(int argc, char **argv) {
    // ---- args -----------------------------------------------------------------------------
    uint64_t seed = 42;
    std::string outRoot = "results";
    size_t numQueries = 200;
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
        } else if (a == "--out") {
            outRoot = next("--out");
        } else if (a == "--queries") {
            numQueries = std::stoull(next("--queries"));
        } else if (a == "--smoke") {
            smoke = true;
        } else if (a == "--help" || a == "-h") {
            std::cout << "usage: benchmark_retrieval [--seed N] [--out DIR] [--queries N] "
                         "[--smoke]\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            return 2;
        }
    }

    // ---- grid -----------------------------------------------------------------------------
    const size_t dim = 64;
    const uint32_t efConstruction = 200; // fixed subset choice (see file header / metadata).
    std::vector<size_t> vectorCounts = smoke ? std::vector<size_t>{1000}
                                             : std::vector<size_t>{10000, 100000};
    const std::vector<uint32_t> ms = {8, 16, 32};
    const std::vector<size_t> efSearches = {16, 32, 64, 128, 256};
    const std::vector<size_t> ks = {10, 50, 200, 500};
    if (smoke) numQueries = 30;
    const size_t warmupQueries = std::min<size_t>(10, numQueries);

    const std::string expId = "hnsw_retrieval-seed" + std::to_string(seed) + "-" + nowStamp();
    const std::filesystem::path outDir = std::filesystem::path(outRoot) / expId;
    std::filesystem::create_directories(outDir);

    std::cerr << "benchmark_retrieval: experiment-id=" << expId << "\n"
              << "  out=" << outDir.string() << "\n"
              << "  dim=" << dim << " efConstruction(fixed)=" << efConstruction
              << " queries=" << numQueries << (smoke ? "  [SMOKE]\n" : "\n");

    rr::Stopwatch wallClock; // total sweep wall time.

    std::vector<Row> rows;
    std::vector<GraphLevelRow> levelRows;

    // ---- sweep ----------------------------------------------------------------------------
    for (size_t vc : vectorCounts) {
        std::cerr << "[vc=" << vc << "] generating dataset + queries ..." << std::endl;
        rr::Rng dataRng = rr::forkRng(seed, "dataset-vc" + std::to_string(vc) + "-d64");
        rr::Rng queryRng = rr::forkRng(seed, "queries-vc" + std::to_string(vc) + "-d64");
        const std::vector<rr::Embedding> data = generateEmbeddings(dataRng, vc, dim);
        const std::vector<rr::Embedding> queries = generateEmbeddings(queryRng, numQueries, dim);

        // Exact ground truth: one index, precompute each query's exact top-500 id list once.
        std::cerr << "[vc=" << vc << "] building ExactVectorIndex ground truth ..." << std::endl;
        rr::ExactVectorIndex exact(dim);
        for (size_t i = 0; i < data.size(); ++i) {
            exact.insert(rr::ReelId{static_cast<uint32_t>(i)}, data[i]);
        }
        const size_t maxK = *std::max_element(ks.begin(), ks.end());
        std::vector<std::vector<uint32_t>> exactTop(queries.size());
        for (size_t q = 0; q < queries.size(); ++q) {
            const auto res = exact.search(queries[q], maxK);
            exactTop[q].reserve(res.size());
            for (const auto &r : res) exactTop[q].push_back(r.reelId.value);
        }

        for (uint32_t m : ms) {
            // Build ONE HNSW graph for this (vc, m). Freed at the end of this scope before the
            // next M, so the RSS delta captures just this graph.
            rr::HNSWConfig cfg;
            cfg.m = m;
            cfg.efConstruction = efConstruction;
            cfg.efSearch = 64; // overridden per-efSearch below via setEfSearch().
            const uint64_t hnswSeed =
                rr::splitmix64(seed ^ rr::fnv1a64("hnsw-vc" + std::to_string(vc) + "-M" +
                                                  std::to_string(m)));

            std::cerr << "[vc=" << vc << " M=" << m << "] building HNSW graph ..." << std::endl;
            auto hnsw = std::make_unique<rr::HNSWVectorIndex>(dim, cfg, hnswSeed);

            const size_t rssBefore = currentRssBytes();
            rr::Stopwatch buildTimer;
            for (size_t i = 0; i < data.size(); ++i) {
                hnsw->insert(rr::ReelId{static_cast<uint32_t>(i)}, data[i]);
            }
            const double buildTimeMs = buildTimer.elapsedMs();
            const size_t rssAfter = currentRssBytes();
            const long long memoryDeltaBytes =
                static_cast<long long>(rssAfter) - static_cast<long long>(rssBefore);
            const double insertThroughput =
                buildTimeMs > 0.0 ? (static_cast<double>(data.size()) / (buildTimeMs / 1000.0))
                                  : 0.0;

            const std::vector<size_t> levelDist = hnsw->getLevelDistribution();
            const size_t numLevels = levelDist.size();
            const size_t maxGraphLevel = numLevels == 0 ? 0 : numLevels - 1;

            levelRows.push_back(GraphLevelRow{vc, m, efConstruction, maxGraphLevel, levelDist,
                                              buildTimeMs, insertThroughput, memoryDeltaBytes});

            std::cerr << "[vc=" << vc << " M=" << m << "] build " << std::fixed
                      << std::setprecision(1) << buildTimeMs << " ms, "
                      << static_cast<long long>(insertThroughput) << " inserts/s, maxLevel="
                      << maxGraphLevel << ", RSS delta "
                      << (memoryDeltaBytes / (1024 * 1024)) << " MB" << std::endl;

            for (size_t ef : efSearches) {
                hnsw->setEfSearch(ef);
                for (size_t k : ks) {
                    // warmup (untimed). search() returns a heap-allocated vector (observable side
                    // effect), so it is not optimized away even though we discard it.
                    for (size_t w = 0; w < warmupQueries; ++w) {
                        auto discard = hnsw->search(queries[w % queries.size()], k);
                        (void)discard;
                    }
                    // timed: one search per query, one latency sample each; recall on same set.
                    std::vector<double> latencies;
                    latencies.reserve(queries.size());
                    double recallSum = 0.0;
                    double returnedSum = 0.0;
                    for (size_t q = 0; q < queries.size(); ++q) {
                        rr::Stopwatch qt;
                        const auto approx = hnsw->search(queries[q], k);
                        latencies.push_back(qt.elapsedMs());

                        // exact top-k ids (slice of precomputed top-maxK)
                        const size_t kk = std::min(k, exactTop[q].size());
                        std::unordered_set<uint32_t> exactSet(
                            exactTop[q].begin(),
                            exactTop[q].begin() + static_cast<std::ptrdiff_t>(kk));
                        size_t overlap = 0;
                        for (const auto &r : approx) {
                            if (exactSet.count(r.reelId.value)) ++overlap;
                        }
                        recallSum += (k > 0) ? (static_cast<double>(overlap) /
                                                static_cast<double>(std::min(k, exactSet.size())))
                                             : 0.0;
                        returnedSum += static_cast<double>(approx.size());
                    }

                    Row row;
                    row.vectorCount = vc;
                    row.dimensions = dim;
                    row.m = m;
                    row.efConstruction = efConstruction;
                    row.efSearch = ef;
                    row.k = k;
                    row.recallAtK = recallSum / static_cast<double>(queries.size());
                    row.avgReturned = returnedSum / static_cast<double>(queries.size());
                    row.p50Ms = percentile(latencies, 50.0);
                    row.p95Ms = percentile(latencies, 95.0);
                    row.p99Ms = percentile(latencies, 99.0);
                    double sum = 0.0;
                    for (double l : latencies) sum += l;
                    row.meanMs = sum / static_cast<double>(latencies.size());
                    row.numQueries = queries.size();
                    row.buildTimeMs = buildTimeMs;
                    row.insertThroughput = insertThroughput;
                    row.memoryDeltaBytes = memoryDeltaBytes;
                    row.maxGraphLevel = maxGraphLevel;
                    row.numLevels = numLevels;
                    rows.push_back(row);
                }
            }
        } // M (hnsw freed here)
    } // vectorCount

    const double totalWallMs = wallClock.elapsedMs();

    // ---- write retrieval_metrics.csv ------------------------------------------------------
    {
        std::ofstream csv(outDir / "retrieval_metrics.csv");
        csv << "vector_count,dimensions,m,ef_construction,ef_search,k,recall_at_k,avg_returned,"
               "query_p50_ms,query_p95_ms,query_p99_ms,query_mean_ms,num_queries,build_time_ms,"
               "insert_throughput_per_sec,memory_delta_bytes,memory_delta_mb,max_graph_level,"
               "num_levels\n";
        csv << std::fixed;
        for (const auto &r : rows) {
            csv << r.vectorCount << ',' << r.dimensions << ',' << r.m << ',' << r.efConstruction
                << ',' << r.efSearch << ',' << r.k << ',' << std::setprecision(6) << r.recallAtK
                << ',' << std::setprecision(3) << r.avgReturned << ',' << std::setprecision(6)
                << r.p50Ms << ',' << r.p95Ms << ',' << r.p99Ms << ',' << r.meanMs << ','
                << r.numQueries << ',' << std::setprecision(3) << r.buildTimeMs << ','
                << std::setprecision(2) << r.insertThroughput << ',' << r.memoryDeltaBytes << ','
                << std::setprecision(3)
                << (static_cast<double>(r.memoryDeltaBytes) / (1024.0 * 1024.0)) << ','
                << r.maxGraphLevel << ',' << r.numLevels << '\n';
        }
    }

    // ---- write graph_levels.csv -----------------------------------------------------------
    {
        std::ofstream csv(outDir / "graph_levels.csv");
        csv << "vector_count,m,ef_construction,max_graph_level,build_time_ms,"
               "insert_throughput_per_sec,memory_delta_mb,level_distribution\n";
        csv << std::fixed;
        for (const auto &g : levelRows) {
            csv << g.vectorCount << ',' << g.m << ',' << g.efConstruction << ',' << g.maxGraphLevel
                << ',' << std::setprecision(3) << g.buildTimeMs << ',' << std::setprecision(2)
                << g.insertThroughput << ',' << std::setprecision(3)
                << (static_cast<double>(g.memoryDeltaBytes) / (1024.0 * 1024.0)) << ',';
            // level distribution as a semicolon-joined list "l0;l1;l2;..."
            for (size_t i = 0; i < g.levelDistribution.size(); ++i) {
                if (i) csv << ';';
                csv << g.levelDistribution[i];
            }
            csv << '\n';
        }
    }

    // ---- write config.json ----------------------------------------------------------------
    {
        std::ofstream j(outDir / "config.json");
        auto arr = [](const auto &v) {
            std::ostringstream o;
            o << '[';
            for (size_t i = 0; i < v.size(); ++i) {
                if (i) o << ',';
                o << v[i];
            }
            o << ']';
            return o.str();
        };
        j << "{\n";
        j << "  \"experiment_id\": \"" << expId << "\",\n";
        j << "  \"name\": \"hnsw_retrieval\",\n";
        j << "  \"seed\": " << seed << ",\n";
        j << "  \"dimensions\": " << dim << ",\n";
        j << "  \"vector_counts\": " << arr(vectorCounts) << ",\n";
        j << "  \"m_values\": " << arr(ms) << ",\n";
        j << "  \"ef_construction_fixed\": " << efConstruction << ",\n";
        j << "  \"ef_search_values\": " << arr(efSearches) << ",\n";
        j << "  \"k_values\": " << arr(ks) << ",\n";
        j << "  \"num_queries\": " << numQueries << ",\n";
        j << "  \"warmup_queries\": " << warmupQueries << ",\n";
        j << "  \"distance_metric\": \"euclidean_on_unit_vectors (cosine-equivalent, D3)\",\n";
        j << "  \"embedding_generation\": \"gaussian-per-component, L2-normalized (D8 rr::Rng)\"\n";
        j << "}\n";
    }

    // ---- write summary.json (headline aggregates) -----------------------------------------
    {
        std::ofstream j(outDir / "summary.json");
        j << std::fixed;
        j << "{\n";
        j << "  \"experiment_id\": \"" << expId << "\",\n";
        j << "  \"total_sweep_wall_ms\": " << std::setprecision(1) << totalWallMs << ",\n";
        j << "  \"total_rows\": " << rows.size() << ",\n";
        j << "  \"per_size\": [\n";
        for (size_t vi = 0; vi < vectorCounts.size(); ++vi) {
            const size_t vc = vectorCounts[vi];
            // recall@10 range across the whole (M x efSearch) sweep for this size
            double r10min = 1e9, r10max = -1e9;
            double p50min = 1e9, p50max = -1e9;
            double thrMin = 1e18, thrMax = -1e18;
            for (const auto &r : rows) {
                if (r.vectorCount != vc) continue;
                if (r.k == 10u) {
                    r10min = std::min(r10min, r.recallAtK);
                    r10max = std::max(r10max, r.recallAtK);
                }
                p50min = std::min(p50min, r.p50Ms);
                p50max = std::max(p50max, r.p50Ms);
                thrMin = std::min(thrMin, r.insertThroughput);
                thrMax = std::max(thrMax, r.insertThroughput);
            }
            j << "    {\n";
            j << "      \"vector_count\": " << vc << ",\n";
            j << "      \"recall_at_10_min\": " << std::setprecision(4) << r10min << ",\n";
            j << "      \"recall_at_10_max\": " << std::setprecision(4) << r10max << ",\n";
            j << "      \"query_p50_ms_min\": " << std::setprecision(6) << p50min << ",\n";
            j << "      \"query_p50_ms_max\": " << std::setprecision(6) << p50max << ",\n";
            j << "      \"insert_throughput_min\": " << std::setprecision(1) << thrMin << ",\n";
            j << "      \"insert_throughput_max\": " << std::setprecision(1) << thrMax << ",\n";
            j << "      \"per_m\": [\n";
            for (size_t mi = 0; mi < ms.size(); ++mi) {
                const uint32_t m = ms[mi];
                const GraphLevelRow *g = nullptr;
                for (const auto &gr : levelRows) {
                    if (gr.vectorCount == vc && gr.m == m) {
                        g = &gr;
                        break;
                    }
                }
                // recall@10 at efSearch extremes for this (vc,m)
                double r10ef16 = -1.0, r10ef256 = -1.0;
                for (const auto &r : rows) {
                    if (r.vectorCount == vc && r.m == m && r.k == 10u) {
                        if (r.efSearch == 16u) r10ef16 = r.recallAtK;
                        if (r.efSearch == 256u) r10ef256 = r.recallAtK;
                    }
                }
                j << "        {\n";
                j << "          \"m\": " << m << ",\n";
                j << "          \"build_time_ms\": " << std::setprecision(1)
                  << (g ? g->buildTimeMs : 0.0) << ",\n";
                j << "          \"insert_throughput_per_sec\": " << std::setprecision(1)
                  << (g ? g->insertThroughput : 0.0) << ",\n";
                j << "          \"max_graph_level\": " << (g ? g->maxGraphLevel : 0) << ",\n";
                j << "          \"memory_delta_mb\": " << std::setprecision(1)
                  << (g ? static_cast<double>(g->memoryDeltaBytes) / (1024.0 * 1024.0) : 0.0)
                  << ",\n";
                j << "          \"recall_at_10_ef16\": " << std::setprecision(4) << r10ef16
                  << ",\n";
                j << "          \"recall_at_10_ef256\": " << std::setprecision(4) << r10ef256
                  << "\n";
                j << "        }" << (mi + 1 < ms.size() ? "," : "") << "\n";
            }
            j << "      ]\n";
            j << "    }" << (vi + 1 < vectorCounts.size() ? "," : "") << "\n";
        }
        j << "  ]\n";
        j << "}\n";
    }

    // ---- write metadata.json --------------------------------------------------------------
    {
        const std::string repoDir = RR_REPO_DIR;
        const std::string vdbDir = RR_VDB_DIR;
        const std::string rrSha = runCommand("git -C '" + repoDir + "' rev-parse HEAD 2>/dev/null");
        const std::string rrDirtyRaw =
            runCommand("git -C '" + repoDir + "' status --porcelain 2>/dev/null");
        const bool rrDirty = !rrDirtyRaw.empty();
        const std::string vdbSha =
            runCommand("git -C '" + vdbDir + "' rev-parse HEAD 2>/dev/null");

#if defined(__APPLE__)
        const std::string cpu = sysctlString("machdep.cpu.brand_string");
        const uint64_t ramBytes = sysctlU64("hw.memsize");
        const uint64_t logicalCores = sysctlU64("hw.ncpu");
        const uint64_t physicalCores = sysctlU64("hw.physicalcpu");
#else
        const std::string cpu = "unknown";
        const uint64_t ramBytes = 0;
        const uint64_t logicalCores = 0;
        const uint64_t physicalCores = 0;
#endif
        std::string osSys, osRel, osVer, osMach;
#if defined(__APPLE__) || defined(__linux__)
        struct utsname uts{};
        if (uname(&uts) == 0) {
            osSys = uts.sysname;
            osRel = uts.release;
            osVer = uts.version;
            osMach = uts.machine;
        }
#endif

        std::ofstream j(outDir / "metadata.json");
        j << "{\n";
        j << "  \"experiment_id\": \"" << expId << "\",\n";
        j << "  \"generated_at\": \"" << nowIso() << "\",\n";
        j << "  \"tool\": \"benchmark_retrieval (ReelRank Phase 1)\",\n";
        j << "  \"git\": {\n";
        j << "    \"reel_rank_sha\": \"" << jsonEscape(rrSha) << "\",\n";
        j << "    \"reel_rank_dir\": \"" << jsonEscape(repoDir) << "\",\n";
        j << "    \"reel_rank_dirty\": " << (rrDirty ? "true" : "false") << ",\n";
        j << "    \"vector_db_sha\": \"" << jsonEscape(vdbSha) << "\",\n";
        j << "    \"vector_db_dir\": \"" << jsonEscape(vdbDir) << "\",\n";
        j << "    \"note\": \"reel-rank SHA is the Phase 0 commit; Phase 1 work (this benchmark, "
             "the vindex adapters, the differential suite) is uncommitted, so the tree is dirty at "
             "benchmark time.\"\n";
        j << "  },\n";
        j << "  \"build\": {\n";
        j << "    \"type\": \"" << jsonEscape(RR_BUILD_TYPE) << "\",\n";
        j << "    \"compiler\": \"" << jsonEscape(RR_COMPILER) << "\",\n";
        j << "    \"cxx_standard\": 20,\n";
        j << "    \"release_flags\": \"-O3 -DNDEBUG -Wall -Wextra -Wpedantic -Werror\"\n";
        j << "  },\n";
        j << "  \"hardware\": {\n";
        j << "    \"cpu_model\": \"" << jsonEscape(cpu) << "\",\n";
        j << "    \"logical_cores\": " << logicalCores << ",\n";
        j << "    \"physical_cores\": " << physicalCores << ",\n";
        j << "    \"ram_bytes\": " << ramBytes << ",\n";
        j << "    \"ram_gib\": " << std::fixed << std::setprecision(1)
          << (static_cast<double>(ramBytes) / (1024.0 * 1024.0 * 1024.0)) << "\n";
        j << "  },\n";
        j << "  \"os\": {\n";
        j << "    \"sysname\": \"" << jsonEscape(osSys) << "\",\n";
        j << "    \"release\": \"" << jsonEscape(osRel) << "\",\n";
        j << "    \"version\": \"" << jsonEscape(osVer) << "\",\n";
        j << "    \"machine\": \"" << jsonEscape(osMach) << "\"\n";
        j << "  },\n";
        j << "  \"execution\": {\n";
        j << "    \"threads_used\": 1,\n";
        j << "    \"note\": \"single-threaded benchmark (D13); logical_cores reported for "
             "provenance only\"\n";
        j << "  },\n";
        j << "  \"sweep\": {\n";
        j << "    \"seed\": " << seed << ",\n";
        j << "    \"dimensions\": " << dim << ",\n";
        j << "    \"vector_counts\": [";
        for (size_t i = 0; i < vectorCounts.size(); ++i) {
            if (i) j << ',';
            j << vectorCounts[i];
        }
        j << "],\n";
        j << "    \"m_values\": [8,16,32],\n";
        j << "    \"ef_construction_fixed\": " << efConstruction << ",\n";
        j << "    \"ef_search_values\": [16,32,64,128,256],\n";
        j << "    \"k_values\": [10,50,200,500],\n";
        j << "    \"num_queries\": " << numQueries << ",\n";
        j << "    \"total_sweep_wall_ms\": " << std::setprecision(1) << totalWallMs << ",\n";
        j << "    \"subset_note\": \"Deliberate subset of the TDD 17.3 grid (per "
             "plan/01-PHASES-FOUNDATION.md Phase 1): efConstruction fixed at 200 (the rr::HNSWConfig "
             "default) rather than swept over {50,100,200,400}; dimensions fixed at 64 (of "
             "{32,64,128,256}); vector counts {10000,100000} with the 1,000,000 case deferred to "
             "Phase 11. M x efSearch x k are swept fully.\"\n";
        j << "  }\n";
        j << "}\n";
    }

    std::cerr << "benchmark_retrieval: DONE in " << std::fixed << std::setprecision(1)
              << (totalWallMs / 1000.0) << " s; wrote " << rows.size() << " rows to "
              << outDir.string() << std::endl;
    // One-line machine-parseable pointer for the caller.
    std::cout << "RESULT_DIR=" << outDir.string() << "\n";
    return 0;
}
