// Graph-statistics and distance-computation-counting tests for the HNSWVectorIndex adapter
// (Phase 11 package B, TDD 17.3 "Graph degree distribution" / "Maximum graph level" / "Distance
// computations per query, if feasible"). Written test-first per TDD 30.
//
// D2: these tests see only rr:: types — HnswGraphStats exposes std types only, never a vector-db
// symbol. The counting metric is an implementation detail confined to src/vindex/*.cpp; the
// contract here is behavioural: (a) enabling counting must not change search results or the graph
// (bit-identical distances and ids), and (b) the counter is 0 when disabled, strictly positive
// after searches when enabled, and resettable.
#include "rr/vindex/hnsw_vector_index.hpp"

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using rr::Embedding;
using rr::HNSWConfig;
using rr::HnswGraphStats;
using rr::HNSWVectorIndex;
using rr::ReelId;
using rr::Rng;

// A normalized isotropic-Gaussian embedding (D8: only rr::Rng, never a std::*_distribution).
Embedding randomUnit(Rng &rng, std::size_t dim) {
    Embedding e(dim);
    for (std::size_t i = 0; i < dim; ++i) {
        e[i] = static_cast<float>(rng.gaussian());
    }
    rr::normalize(e);
    return e;
}

// Fill an index with n identical-per-seed vectors; returns the vectors so a caller can build a
// second index with the SAME data.
std::vector<Embedding> fill(HNSWVectorIndex &index, std::uint64_t dataSeed, std::size_t dim,
                            std::uint32_t n) {
    Rng rng = rr::forkRng(dataSeed, "stats-data");
    std::vector<Embedding> data;
    data.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        Embedding e = randomUnit(rng, dim);
        index.insert(ReelId{i}, e);
        data.push_back(std::move(e));
    }
    return data;
}

} // namespace

// --- graph statistics invariants -------------------------------------------------------------

TEST(HnswVectorIndexStatsTest, EmptyIndexGraphStatsAreEmpty) {
    HNSWVectorIndex index(8, HNSWConfig{}, 42);
    const HnswGraphStats s = index.graphStats();
    EXPECT_EQ(s.nodeCount, 0u);
    EXPECT_EQ(s.maxLevel, -1); // empty graph => top level -1 (matches vector-db getMaxLevel()).
    EXPECT_TRUE(s.levelDistribution.empty());
    EXPECT_TRUE(s.degreeHistogramLevel0.empty());
}

TEST(HnswVectorIndexStatsTest, LevelDistributionSumsToNodeCountAndMatchesMaxLevel) {
    HNSWConfig cfg; // M=16 => M0=32.
    const std::size_t dim = 24;
    const std::uint32_t n = 400;
    HNSWVectorIndex index(dim, cfg, 12345);
    fill(index, 777, dim, n);

    const HnswGraphStats s = index.graphStats();
    EXPECT_EQ(s.nodeCount, static_cast<std::size_t>(n));
    EXPECT_EQ(s.nodeCount, index.size());

    std::size_t total = 0;
    for (std::size_t c : s.levelDistribution) {
        total += c;
    }
    EXPECT_EQ(total, static_cast<std::size_t>(n)) << "level distribution must sum to node count";

    // maxLevel is consistent with the distribution length: [l] = count of nodes whose TOP level
    // == l, so the top populated level is size()-1.
    ASSERT_FALSE(s.levelDistribution.empty());
    EXPECT_EQ(s.maxLevel, static_cast<int>(s.levelDistribution.size()) - 1);
    EXPECT_GT(s.levelDistribution[0], 0u); // most nodes live only on level 0.
}

TEST(HnswVectorIndexStatsTest, DegreeHistogramBoundedByM0AndSumsToNodeCount) {
    // Use a small M so the M0 bound (2*M) is tight and easy to exceed if the wiring is wrong.
    HNSWConfig cfg;
    cfg.m = 8; // M0 == 16.
    const std::size_t dim = 16;
    const std::uint32_t n = 500;
    HNSWVectorIndex index(dim, cfg, 2024);
    fill(index, 99, dim, n);

    const HnswGraphStats s = index.graphStats();
    const std::size_t m0 = 2u * cfg.m;

    // degreeHistogramLevel0[d] = number of nodes with exactly d level-0 neighbours; degree cannot
    // exceed M0, so the histogram has at most M0+1 buckets (indices 0..M0).
    EXPECT_LE(s.degreeHistogramLevel0.size(), m0 + 1) << "a level-0 degree exceeds M0 = " << m0;

    std::size_t total = 0;
    for (std::size_t c : s.degreeHistogramLevel0) {
        total += c;
    }
    EXPECT_EQ(total, static_cast<std::size_t>(n))
        << "every node contributes exactly one level-0 degree bucket";
}

// --- distance-computation counting -----------------------------------------------------------

TEST(HnswVectorIndexStatsTest, CounterZeroWhenDisabled) {
    const std::size_t dim = 32;
    HNSWVectorIndex index(dim, HNSWConfig{}, 42); // counting OFF (default).
    fill(index, 5, dim, 300);
    Rng qrng = rr::forkRng(6, "q");
    for (int i = 0; i < 20; ++i) {
        auto r = index.search(randomUnit(qrng, dim), 10);
        (void)r;
    }
    EXPECT_EQ(index.distanceComputations(), 0u)
        << "distance counter must read 0 when counting is not enabled";
}

TEST(HnswVectorIndexStatsTest, CounterStrictlyPositiveAfterSearchesWhenEnabled) {
    const std::size_t dim = 32;
    HNSWVectorIndex index(dim, HNSWConfig{}, 42, /*countDistanceComps=*/true);
    fill(index, 5, dim, 300);

    // Build itself computes distances; reset so we measure the search phase in isolation.
    index.resetDistanceCounter();
    EXPECT_EQ(index.distanceComputations(), 0u);

    Rng qrng = rr::forkRng(6, "q");
    for (int i = 0; i < 20; ++i) {
        auto r = index.search(randomUnit(qrng, dim), 10);
        (void)r;
    }
    EXPECT_GT(index.distanceComputations(), 0u)
        << "searches must register distance computations when counting is enabled";
}

TEST(HnswVectorIndexStatsTest, BuildAlsoCountsDistancesThenResetClearsIt) {
    const std::size_t dim = 16;
    HNSWVectorIndex index(dim, HNSWConfig{}, 42, /*countDistanceComps=*/true);
    fill(index, 5, dim, 400);
    EXPECT_GT(index.distanceComputations(), 0u) << "insertion computes distances too";
    index.resetDistanceCounter();
    EXPECT_EQ(index.distanceComputations(), 0u);
}

// The load-bearing safety property: turning counting ON must not perturb search results or the
// graph. Distances are produced by the identical Euclidean math (the counter only decorates it),
// so results are BIT-identical across >= 5 seeds.
TEST(HnswVectorIndexStatsTest, CountingDoesNotChangeSearchResultsAcrossSeeds) {
    const std::size_t dim = 48;
    const std::uint32_t n = 800;
    const std::vector<std::size_t> ks = {10, 50};

    for (std::uint64_t seed : {1ull, 2ull, 3ull, 4ull, 5ull, 6ull}) {
        HNSWConfig cfg;
        cfg.m = 16;
        HNSWVectorIndex off(dim, cfg, seed, /*countDistanceComps=*/false);
        HNSWVectorIndex on(dim, cfg, seed, /*countDistanceComps=*/true);
        // Identical data (same dataSeed) inserted in identical order into both.
        fill(off, seed ^ 0xABCDEFull, dim, n);
        fill(on, seed ^ 0xABCDEFull, dim, n);

        Rng qrng = rr::forkRng(seed, "bit-equality-queries");
        for (int q = 0; q < 12; ++q) {
            const Embedding query = randomUnit(qrng, dim);
            for (std::size_t k : ks) {
                const auto ra = off.search(query, k);
                const auto rb = on.search(query, k);
                ASSERT_EQ(ra.size(), rb.size())
                    << "seed=" << seed << " k=" << k << " result-count divergence";
                for (std::size_t i = 0; i < ra.size(); ++i) {
                    EXPECT_EQ(ra[i].reelId, rb[i].reelId)
                        << "seed=" << seed << " rank=" << i << " id divergence";
                    // Bit-identical distance: same EuclideanDistance code path, counting or not.
                    EXPECT_EQ(ra[i].distance, rb[i].distance)
                        << "seed=" << seed << " rank=" << i << " distance not bit-identical";
                    EXPECT_EQ(ra[i].similarity, rb[i].similarity)
                        << "seed=" << seed << " rank=" << i << " similarity not bit-identical";
                }
            }
        }
        // The counting index recorded work; the plain one recorded nothing.
        EXPECT_GT(on.distanceComputations(), 0u) << "seed=" << seed;
        EXPECT_EQ(off.distanceComputations(), 0u) << "seed=" << seed;
    }
}

// graphStats must be identical whether or not counting is enabled (the graph is the same).
TEST(HnswVectorIndexStatsTest, GraphStatsIdenticalWithAndWithoutCounting) {
    const std::size_t dim = 32;
    const std::uint32_t n = 600;
    HNSWVectorIndex off(dim, HNSWConfig{}, 909, /*countDistanceComps=*/false);
    HNSWVectorIndex on(dim, HNSWConfig{}, 909, /*countDistanceComps=*/true);
    fill(off, 4242, dim, n);
    fill(on, 4242, dim, n);

    const HnswGraphStats a = off.graphStats();
    const HnswGraphStats b = on.graphStats();
    EXPECT_EQ(a.nodeCount, b.nodeCount);
    EXPECT_EQ(a.maxLevel, b.maxLevel);
    EXPECT_EQ(a.levelDistribution, b.levelDistribution);
    EXPECT_EQ(a.degreeHistogramLevel0, b.degreeHistogramLevel0);
}
