#include "rr/vindex/exact_vector_index.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"

using rr::Embedding;
using rr::ExactVectorIndex;
using rr::ReelId;
using rr::similarityFromEuclidean;
using rr::VectorSearchResult;

namespace {

ReelId reel(uint32_t v) { return ReelId{v}; }

} // namespace

TEST(ExactVectorIndexTest, EmptyIndexSearchReturnsEmpty) {
    ExactVectorIndex index(2);
    EXPECT_EQ(index.size(), 0u);
    const auto results = index.search(Embedding{1.0f, 0.0f}, 5);
    EXPECT_TRUE(results.empty());
}

TEST(ExactVectorIndexTest, KZeroReturnsEmpty) {
    ExactVectorIndex index(2);
    index.insert(reel(1), Embedding{1.0f, 0.0f});
    index.insert(reel(2), Embedding{0.0f, 1.0f});
    const auto results = index.search(Embedding{1.0f, 0.0f}, 0);
    EXPECT_TRUE(results.empty());
}

TEST(ExactVectorIndexTest, KGreaterThanSizeReturnsAllSorted) {
    ExactVectorIndex index(2);
    // Distances from origin query: id 30 -> 3, id 10 -> 1, id 20 -> 2. Inserted out of order.
    index.insert(reel(30), Embedding{3.0f, 0.0f});
    index.insert(reel(10), Embedding{1.0f, 0.0f});
    index.insert(reel(20), Embedding{0.0f, 2.0f});

    const auto results = index.search(Embedding{0.0f, 0.0f}, 100);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].reelId, reel(10));
    EXPECT_EQ(results[1].reelId, reel(20));
    EXPECT_EQ(results[2].reelId, reel(30));
    EXPECT_NEAR(results[0].distance, 1.0f, 1e-6f);
    EXPECT_NEAR(results[1].distance, 2.0f, 1e-6f);
    EXPECT_NEAR(results[2].distance, 3.0f, 1e-6f);
}

TEST(ExactVectorIndexTest, ExactSelfMatchIsTopAtZeroDistance) {
    ExactVectorIndex index(3);
    const Embedding target{0.0f, 3.0f, 4.0f};
    index.insert(reel(1), Embedding{5.0f, 0.0f, 0.0f});
    index.insert(reel(2), target);
    index.insert(reel(3), Embedding{0.0f, 0.0f, 1.0f});

    const auto results = index.search(target, 3);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].reelId, reel(2));
    EXPECT_NEAR(results[0].distance, 0.0f, 1e-6f);
    // similarityFromEuclidean(0) == 1.
    EXPECT_NEAR(results[0].similarity, 1.0f, 1e-6f);
}

TEST(ExactVectorIndexTest, OrderingAscendingByDistanceWithSimilarity) {
    ExactVectorIndex index(2);
    index.insert(reel(1), Embedding{1.0f, 0.0f}); // distance 1 from origin
    index.insert(reel(2), Embedding{2.0f, 0.0f}); // distance 2 from origin
    index.insert(reel(3), Embedding{0.0f, 3.0f}); // distance 3 from origin

    const auto results = index.search(Embedding{0.0f, 0.0f}, 3);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].reelId, reel(1));
    EXPECT_EQ(results[1].reelId, reel(2));
    EXPECT_EQ(results[2].reelId, reel(3));
    // Distances must be non-decreasing.
    EXPECT_LE(results[0].distance, results[1].distance);
    EXPECT_LE(results[1].distance, results[2].distance);
    // Similarity mirrors D3: 1 - d^2/2. For distance 2 -> -1.
    EXPECT_NEAR(results[1].similarity, similarityFromEuclidean(2.0f), 1e-6f);
    EXPECT_NEAR(results[1].similarity, -1.0f, 1e-6f);
}

TEST(ExactVectorIndexTest, TiesBrokenByAscendingReelId) {
    ExactVectorIndex index(2);
    // {1,0} and {0,1} are both at distance 1 from the origin query (bit-identical distances).
    // Insert the higher id first so hash/insertion order cannot accidentally satisfy the test.
    index.insert(reel(7), Embedding{0.0f, 1.0f});
    index.insert(reel(3), Embedding{1.0f, 0.0f});

    const auto results = index.search(Embedding{0.0f, 0.0f}, 2);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].reelId, reel(3));
    EXPECT_EQ(results[1].reelId, reel(7));
    EXPECT_FLOAT_EQ(results[0].distance, results[1].distance);
}

TEST(ExactVectorIndexTest, DuplicateIdInsertThrows) {
    ExactVectorIndex index(2);
    index.insert(reel(1), Embedding{1.0f, 0.0f});
    EXPECT_THROW(index.insert(reel(1), Embedding{0.0f, 1.0f}), std::invalid_argument);
    EXPECT_EQ(index.size(), 1u);
}

TEST(ExactVectorIndexTest, DimensionMismatchInsertThrows) {
    ExactVectorIndex index(3);
    EXPECT_THROW(index.insert(reel(1), Embedding{1.0f, 0.0f}), std::invalid_argument);
    EXPECT_THROW(index.insert(reel(2), Embedding{1.0f, 0.0f, 0.0f, 0.0f}), std::invalid_argument);
    EXPECT_EQ(index.size(), 0u);
}

TEST(ExactVectorIndexTest, NonFiniteEmbeddingInsertThrows) {
    ExactVectorIndex index(2);
    const Embedding nan{1.0f, std::numeric_limits<float>::quiet_NaN()};
    const Embedding inf{std::numeric_limits<float>::infinity(), 0.0f};
    EXPECT_THROW(index.insert(reel(1), nan), std::invalid_argument);
    EXPECT_THROW(index.insert(reel(2), inf), std::invalid_argument);
    EXPECT_EQ(index.size(), 0u);
}

TEST(ExactVectorIndexTest, SizeReflectsInsertCount) {
    ExactVectorIndex index(2);
    EXPECT_EQ(index.size(), 0u);
    index.insert(reel(1), Embedding{1.0f, 0.0f});
    EXPECT_EQ(index.size(), 1u);
    index.insert(reel(2), Embedding{0.0f, 1.0f});
    index.insert(reel(3), Embedding{1.0f, 1.0f});
    EXPECT_EQ(index.size(), 3u);
}
