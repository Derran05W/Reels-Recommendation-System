#include "rr/evaluation/retrieval_evaluator.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/reel.hpp"
#include "rr/recommendation/vector_index.hpp"
#include "rr/vindex/exact_vector_index.hpp"

namespace {

constexpr std::size_t kDim = 64;

// A single 1.0 at position `axis` (already unit length) — an orthonormal basis vector.
rr::Embedding basis(std::size_t axis) {
    rr::Embedding e(kDim, 0.0f);
    e[axis] = 1.0f;
    return e;
}

rr::Reel makeReel(uint32_t id, std::size_t axis, bool active = true) {
    rr::Reel reel{};
    reel.id = rr::ReelId{id};
    reel.active = active;
    reel.embedding = basis(axis);
    return reel;
}

// Reels 0..n-1 on distinct orthonormal axes 0..n-1.
std::vector<rr::Reel> basisReels(uint32_t n) {
    std::vector<rr::Reel> reels;
    for (uint32_t i = 0; i < n; ++i) {
        reels.push_back(makeReel(i, i));
    }
    return reels;
}

// Hand-written degraded ANN index: search() ignores the query and returns a fixed, deliberately
// wrong ordering so recall and distance error are fully hand-computable.
class DegradedIndex : public rr::VectorIndex {
  public:
    explicit DegradedIndex(std::vector<rr::VectorSearchResult> fixed) : fixed_(std::move(fixed)) {}
    void insert(const rr::ReelId &, const rr::Embedding &) override {}
    std::vector<rr::VectorSearchResult> search(const rr::Embedding &,
                                               std::size_t k) const override {
        std::vector<rr::VectorSearchResult> out = fixed_;
        if (k < out.size()) {
            out.resize(k);
        }
        return out;
    }
    std::size_t size() const override { return fixed_.size(); }

  private:
    std::vector<rr::VectorSearchResult> fixed_;
};

} // namespace

// An exact ANN index vs the exact ground truth: perfect recall, zero distance error, at any corpus.
TEST(RetrievalEvaluatorTest, ExactAnnMatchesGroundTruthPerfectly) {
    const std::vector<rr::Reel> reels = basisReels(20);
    const rr::RetrievalEvaluator evaluator(kDim, reels);

    // Build a second exact index over the same reels as the "ANN" side.
    rr::ExactVectorIndex ann(kDim);
    for (const rr::Reel &r : reels) {
        ann.insert(r.id, r.embedding);
    }

    const rr::RetrievalSample s = evaluator.evaluate(ann, basis(0));
    EXPECT_DOUBLE_EQ(s.recallAt10, 1.0);
    EXPECT_DOUBLE_EQ(s.recallAt50, 1.0);
    EXPECT_DOUBLE_EQ(s.distanceError, 0.0);
    EXPECT_EQ(s.exactK, 20u);
}

// A deliberately-degraded ANN index -> hand-computed recall fractions and distance error.
//
// Ground truth: reels 0..59 on distinct axes, query = axis 61 (not used by any reel), so every reel
// is orthogonal to the query and sits at distance sqrt(2). Exact order is therefore the tie-break
// order (ascending ReelId): 0, 1, 2, ..., 59. Exact top-10 ids = {0..9}; exact top-50 ids =
// {0..49}; every exact distance = sqrt(2).
TEST(RetrievalEvaluatorTest, DegradedAnnYieldsKnownRecallAndDistanceError) {
    const std::vector<rr::Reel> reels = basisReels(60);
    const rr::RetrievalEvaluator evaluator(kDim, reels);

    // Fixed ANN result list of exactly kEval(=50) entries, all with distance 0.0 (so it disagrees
    // with every exact distance of sqrt(2), giving distanceError == sqrt(2)).
    //   positions 0..9 : ids 0,1,2,3,4,5, 50,51,52,53   (6 of the exact top-10 {0..9}) -> recall@10
    //   positions 10..49: ids 6,7,...,45                 (40 consecutive ids)
    std::vector<uint32_t> annIds = {0, 1, 2, 3, 4, 5, 50, 51, 52, 53};
    for (uint32_t id = 6; id <= 45; ++id) {
        annIds.push_back(id);
    }
    ASSERT_EQ(annIds.size(), 50u);
    std::vector<rr::VectorSearchResult> fixed;
    for (uint32_t id : annIds) {
        fixed.push_back(rr::VectorSearchResult{rr::ReelId{id}, 0.0f, 1.0f});
    }
    const DegradedIndex ann(std::move(fixed));

    const rr::RetrievalSample s = evaluator.evaluate(ann, basis(61));

    // recall@10 = |{0,1,2,3,4,5,50,51,52,53} intersect {0..9}| / 10 = 6/10.
    EXPECT_DOUBLE_EQ(s.recallAt10, 0.6);
    // ANN top-50 set = {0..45} union {50,51,52,53}; exact top-50 = {0..49}; intersection = {0..45}.
    EXPECT_DOUBLE_EQ(s.recallAt50, 46.0 / 50.0);
    // Every exact distance is sqrt(2), every ANN distance is 0 -> mean |0 - sqrt(2)| = sqrt(2).
    EXPECT_NEAR(s.distanceError, std::sqrt(2.0), 1e-5);
    EXPECT_EQ(s.exactK, 50u);
}

// k > corpus size: the denominator collapses to min(k, size), and an exact ANN still scores 1.0.
TEST(RetrievalEvaluatorTest, CorpusSmallerThanKUsesMinDenominator) {
    const std::vector<rr::Reel> reels = basisReels(5);
    const rr::RetrievalEvaluator evaluator(kDim, reels);

    rr::ExactVectorIndex ann(kDim);
    for (const rr::Reel &r : reels) {
        ann.insert(r.id, r.embedding);
    }

    const rr::RetrievalSample s = evaluator.evaluate(ann, basis(0));
    EXPECT_DOUBLE_EQ(s.recallAt10, 1.0); // denominator min(10, 5) = 5, all 5 recovered
    EXPECT_DOUBLE_EQ(s.recallAt50, 1.0); // denominator min(50, 5) = 5
    EXPECT_DOUBLE_EQ(s.distanceError, 0.0);
    EXPECT_EQ(s.exactK, 5u);
}

// Inactive reels are excluded from the ground truth (mirrors ExactVectorRecommender's index).
TEST(RetrievalEvaluatorTest, InactiveReelsExcludedFromGroundTruth) {
    std::vector<rr::Reel> reels = basisReels(6);
    reels[3].active = false;
    reels[5].active = false;
    const rr::RetrievalEvaluator evaluator(kDim, reels);
    EXPECT_EQ(evaluator.groundTruthSize(), 4u);
}

// --- Phase 8: appendReels grows the ground truth after a mid-simulation reel injection -----------

// Ground truth grows by exactly the number of appended ACTIVE reels; inactive appended reels are
// skipped (same rule as the constructor).
TEST(RetrievalEvaluatorTest, AppendReelsGrowsGroundTruth) {
    std::vector<rr::Reel> reels = basisReels(10); // ground truth built over 10 active reels
    rr::RetrievalEvaluator evaluator(kDim, reels);
    ASSERT_EQ(evaluator.groundTruthSize(), 10u);

    const std::size_t firstNew = reels.size();
    reels.push_back(makeReel(10, 10));                   // active
    reels.push_back(makeReel(11, 11, /*active=*/false)); // inactive -> skipped
    reels.push_back(makeReel(12, 12));                   // active
    evaluator.appendReels(reels, firstNew);

    EXPECT_EQ(evaluator.groundTruthSize(), 12u); // 10 + 2 active appended
}

// After append the exact recall math is still exact: an ANN index built over the SAME grown catalog
// scores perfect recall, and the appended reels participate as genuine neighbours.
TEST(RetrievalEvaluatorTest, RecallExactAfterAppend) {
    std::vector<rr::Reel> reels = basisReels(20);
    rr::RetrievalEvaluator evaluator(kDim, reels);

    const std::size_t firstNew = reels.size();
    for (uint32_t i = 20; i < 30; ++i) {
        reels.push_back(makeReel(i, i)); // 10 more active reels on fresh axes
    }
    evaluator.appendReels(reels, firstNew);
    ASSERT_EQ(evaluator.groundTruthSize(), 30u);

    // Build an exact "ANN" index over the full grown catalog.
    rr::ExactVectorIndex ann(kDim);
    for (const rr::Reel &r : reels) {
        ann.insert(r.id, r.embedding);
    }

    // Query aligned to a freshly-appended axis (25): it must be the exact nearest neighbour, and an
    // exact ANN reproduces the ground truth perfectly.
    const rr::RetrievalSample s = evaluator.evaluate(ann, basis(25));
    EXPECT_DOUBLE_EQ(s.recallAt10, 1.0);
    EXPECT_DOUBLE_EQ(s.recallAt50, 1.0);
    EXPECT_DOUBLE_EQ(s.distanceError, 0.0);
    EXPECT_EQ(s.exactK, 30u);
}
