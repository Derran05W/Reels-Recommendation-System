// Consumer-side correctness tests for the HNSWVectorIndex adapter (TDD 17.1, mirrored from the
// consumer side per Phase 1 task 2). These validate the rr::VectorIndex contract and the D2/D3/D4
// adapter behaviour (validation-before-insert, decimal-string key conversion, Euclidean->cosine
// similarity), NOT vector-db's internal recall (that is a later benchmark package's job).
#include "rr/vindex/hnsw_vector_index.hpp"

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {

using rr::Embedding;
using rr::HNSWConfig;
using rr::HNSWVectorIndex;
using rr::ReelId;
using rr::Rng;
using rr::VectorSearchResult;

// A normalized random embedding drawn from an isotropic Gaussian (D8: only rr::Rng, never a
// std::*_distribution). Direction is uniform on the unit sphere after normalization.
Embedding randomUnit(Rng &rng, std::size_t dim) {
    Embedding e(dim);
    for (std::size_t i = 0; i < dim; ++i) {
        e[i] = static_cast<float>(rng.gaussian());
    }
    rr::normalize(e);
    return e;
}

HNSWConfig defaultConfig() {
    return HNSWConfig{}; // m=16, efConstruction=200, efSearch=64
}

// Every result must be internally consistent: similarity is exactly the D3 conversion of its
// distance, and the fields are finite.
void expectWellFormed(const VectorSearchResult &r) {
    EXPECT_TRUE(std::isfinite(r.distance));
    EXPECT_GE(r.distance, -1e-4f); // Euclidean distance is non-negative (tiny fp slack).
    EXPECT_TRUE(std::isfinite(r.similarity));
    EXPECT_FLOAT_EQ(r.similarity, rr::similarityFromEuclidean(r.distance));
}

} // namespace

TEST(HNSWVectorIndexTest, EmptyIndexSearchReturnsEmpty) {
    HNSWVectorIndex index(8, defaultConfig(), 42);
    Rng rng = rr::forkRng(1, "empty");
    EXPECT_EQ(index.size(), 0u);
    auto results = index.search(randomUnit(rng, 8), 5);
    EXPECT_TRUE(results.empty());
}

TEST(HNSWVectorIndexTest, FirstInsertionThenSingleElementSearchFindsIt) {
    HNSWVectorIndex index(16, defaultConfig(), 42);
    Rng rng = rr::forkRng(2, "single");
    Embedding e = randomUnit(rng, 16);
    index.insert(ReelId{7u}, e);
    EXPECT_EQ(index.size(), 1u);

    auto results = index.search(e, 5); // k > size => all elements
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].reelId, ReelId{7u});
    // Self-match: distance ~0 => similarity ~1.
    EXPECT_NEAR(results[0].distance, 0.0f, 1e-4f);
    EXPECT_NEAR(results[0].similarity, 1.0f, 1e-4f);
    expectWellFormed(results[0]);
}

TEST(HNSWVectorIndexTest, KZeroReturnsEmpty) {
    HNSWVectorIndex index(8, defaultConfig(), 42);
    Rng rng = rr::forkRng(3, "kzero");
    for (std::uint32_t i = 0; i < 10; ++i) {
        index.insert(ReelId{i}, randomUnit(rng, 8));
    }
    auto results = index.search(randomUnit(rng, 8), 0);
    EXPECT_TRUE(results.empty());
}

TEST(HNSWVectorIndexTest, VeryLargeKReturnsAllElements) {
    HNSWVectorIndex index(8, defaultConfig(), 42);
    Rng rng = rr::forkRng(4, "largek");
    const std::uint32_t n = 25;
    std::unordered_set<std::uint32_t> inserted;
    for (std::uint32_t i = 0; i < n; ++i) {
        index.insert(ReelId{i}, randomUnit(rng, 8));
        inserted.insert(i);
    }
    auto results = index.search(randomUnit(rng, 8), 1000);
    ASSERT_EQ(results.size(), static_cast<std::size_t>(n));

    // Every inserted id comes back exactly once, distances are ascending, all well-formed.
    std::unordered_set<std::uint32_t> seen;
    float prev = -std::numeric_limits<float>::infinity();
    for (const auto &r : results) {
        expectWellFormed(r);
        EXPECT_TRUE(inserted.count(r.reelId.value) == 1);
        EXPECT_TRUE(seen.insert(r.reelId.value).second) << "duplicate id in results";
        EXPECT_GE(r.distance, prev) << "results not in ascending distance order";
        prev = r.distance;
    }
}

TEST(HNSWVectorIndexTest, DuplicateIdInsertThrows) {
    HNSWVectorIndex index(8, defaultConfig(), 42);
    Rng rng = rr::forkRng(5, "dup");
    index.insert(ReelId{99u}, randomUnit(rng, 8));
    // Propagated untouched from vector-db (D2: adapters do not pre-check duplicate keys).
    EXPECT_THROW(index.insert(ReelId{99u}, randomUnit(rng, 8)), std::invalid_argument);
    EXPECT_EQ(index.size(), 1u);
}

TEST(HNSWVectorIndexTest, DimensionMismatchInsertThrows) {
    HNSWVectorIndex index(8, defaultConfig(), 42);
    Embedding wrong(4, 0.5f); // wrong dimension, finite
    EXPECT_THROW(index.insert(ReelId{1u}, wrong), std::invalid_argument);
    EXPECT_EQ(index.size(), 0u); // rejected before any vector-db mutation
}

TEST(HNSWVectorIndexTest, NonFiniteEmbeddingInsertThrows) {
    HNSWVectorIndex index(8, defaultConfig(), 42);
    Embedding nan(8, 0.35f);
    nan[3] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(index.insert(ReelId{1u}, nan), std::invalid_argument);

    Embedding inf(8, 0.35f);
    inf[5] = std::numeric_limits<float>::infinity();
    EXPECT_THROW(index.insert(ReelId{2u}, inf), std::invalid_argument);

    EXPECT_EQ(index.size(), 0u); // both rejected before any vector-db mutation
}

TEST(HNSWVectorIndexTest, DeterministicUnderFixedSeed) {
    const std::size_t dim = 32;
    const std::uint32_t n = 300;
    const std::uint64_t seed = 20260710;

    auto build = [&](HNSWVectorIndex &index) {
        Rng rng = rr::forkRng(6, "determinism"); // identical vectors, identical order.
        for (std::uint32_t i = 0; i < n; ++i) {
            index.insert(ReelId{i}, randomUnit(rng, dim));
        }
    };

    HNSWVectorIndex a(dim, defaultConfig(), seed);
    HNSWVectorIndex b(dim, defaultConfig(), seed);
    build(a);
    build(b);

    Rng qrng = rr::forkRng(7, "determinism-queries");
    for (int q = 0; q < 5; ++q) {
        Embedding query = randomUnit(qrng, dim);
        auto ra = a.search(query, 10);
        auto rb = b.search(query, 10);
        ASSERT_EQ(ra.size(), rb.size());
        for (std::size_t i = 0; i < ra.size(); ++i) {
            EXPECT_EQ(ra[i].reelId, rb[i].reelId) << "divergent id at rank " << i;
            EXPECT_FLOAT_EQ(ra[i].distance, rb[i].distance) << "divergent distance at rank " << i;
        }
    }
}

TEST(HNSWVectorIndexTest, SearchAfterLargeBatchInsertionIsWellShaped) {
    const std::size_t dim = 64;
    const std::uint32_t n = 10000;
    HNSWVectorIndex index(dim, defaultConfig(), 42);
    Rng rng = rr::forkRng(8, "batch10k");

    Embedding probe; // remember one inserted vector to query with.
    for (std::uint32_t i = 0; i < n; ++i) {
        Embedding e = randomUnit(rng, dim);
        if (i == n / 2) {
            probe = e;
        }
        index.insert(ReelId{i}, e);
    }
    ASSERT_EQ(index.size(), static_cast<std::size_t>(n));

    const std::size_t k = 10;
    auto results = index.search(probe, k);
    ASSERT_EQ(results.size(), k); // correctly shaped (sanity, not a recall assertion)

    float prev = -std::numeric_limits<float>::infinity();
    std::unordered_set<std::uint32_t> seen;
    for (const auto &r : results) {
        expectWellFormed(r);
        EXPECT_LT(r.reelId.value, n) << "id out of inserted range";
        EXPECT_TRUE(seen.insert(r.reelId.value).second) << "duplicate id in results";
        EXPECT_GE(r.distance, prev) << "results not in ascending distance order";
        prev = r.distance;
    }
}

TEST(HNSWVectorIndexTest, SetEfSearchIsANoCrashPassthrough) {
    const std::size_t dim = 32;
    HNSWVectorIndex index(dim, defaultConfig(), 42);
    Rng rng = rr::forkRng(9, "efsearch");
    for (std::uint32_t i = 0; i < 500; ++i) {
        index.insert(ReelId{i}, randomUnit(rng, dim));
    }
    Embedding query = randomUnit(rng, dim);

    auto before = index.search(query, 10);
    index.setEfSearch(256); // widen the search beam.
    auto after = index.search(query, 10);

    ASSERT_EQ(before.size(), 10u);
    ASSERT_EQ(after.size(), 10u);
    for (const auto &r : after) {
        expectWellFormed(r);
    }
    // A wider beam can only find neighbours at least as close as a narrower one (never worse).
    EXPECT_LE(after.front().distance, before.front().distance + 1e-5f);
}

TEST(HNSWVectorIndexTest, GetLevelDistributionNonEmptyAfterInserts) {
    const std::size_t dim = 32;
    const std::uint32_t n = 200;
    HNSWVectorIndex index(dim, defaultConfig(), 42);
    Rng rng = rr::forkRng(10, "levels");
    for (std::uint32_t i = 0; i < n; ++i) {
        index.insert(ReelId{i}, randomUnit(rng, dim));
    }

    auto dist = index.getLevelDistribution();
    ASSERT_FALSE(dist.empty());
    // [l] = count of nodes whose TOP level == l, so the counts sum to the node count.
    std::size_t total = 0;
    for (std::size_t c : dist) {
        total += c;
    }
    EXPECT_EQ(total, static_cast<std::size_t>(n));
    EXPECT_GT(dist[0], 0u); // most nodes live only on level 0.
}
