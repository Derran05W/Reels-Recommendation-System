#pragma once

#include <cstdint>
#include <vector>

#include "rr/domain/candidate.hpp"
#include "rr/domain/recommendation.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/clock.hpp"
#include "rr/recommendation/candidate_generator.hpp"

namespace rr {

// TDD 12.5 fresh candidate source: recently-created reels, exposed as a CandidateGenerator for the
// orchestrated pipeline (TDD 13). Helps solve new-video cold start by surfacing reels that the
// engagement-driven sources (popular/trending) cannot yet rank.
//
// QUALIFICATION at request time t: a reel qualifies iff it is active, has a non-empty embedding,
// and createdAt >= t - freshWindowSeconds (the recency window). The comparison is done in double
// so the unsigned Timestamp never underflows: when freshWindowSeconds >= t the cutoff is <= 0 and
// EVERY reel qualifies. A future-dated reel (createdAt > t, reachable only in hand-built tests)
// also qualifies — it trivially clears the lower bound, consistent with rr::freshnessScore
// clamping such a reel to age 0 (maximally fresh). There is no engagement floor, so on a cold
// catalog Fresh returns the newest reels regardless of interactions (contrast Trending).
//
// ORDER: a deterministic TOTAL order — createdAt DESCENDING (newest first), ties broken by
// ascending ReelId — capped at `count`. May return fewer than `count` (possibly zero) when the
// window admits fewer qualifying reels.
//
// OPTIONAL TOPIC PROXIMITY (TDD 12.5 "optionally limit candidates to topics near the user's
// estimated interests"): when `topicProximity` is true, a reel additionally qualifies only if
// cos(effectivePreference(user), reel.embedding) >= kTopicProximityMinSimilarity. Default OFF
// (the phase-8 experiments do not exercise it); the threshold is a documented named constant, not
// config surface. The proximity check assumes a same-dimension user estimate (the same contract
// every non-vector source relies on for its similarity field).
//
// COST: O(catalog) per request (one qualifying scan + a partial_sort of the qualifying reels).
// Accepted here for the same reason as popular/trending (TDD 13); the constant factor is kept
// tight via a reused scratch buffer with no per-reel allocation.
//
// FILTERING: skips inactive reels and empty embeddings during the scan; dedup / seen / pool-cap
// are the Orchestrator's job (TDD 13). CANDIDATE FIELDS are filled exactly like the other
// non-vector sources (reelId, source=Fresh, genuine retrievalSimilarity =
// cos(effectivePreference(user), reel.embedding), D3-inverse distance) so fresh candidates are not
// starved at the Orchestrator's similarity-ordered pool cap and feed the ranker's similarity
// feature.
class FreshCandidateSource final : public CandidateGenerator {
  public:
    // Minimum cosine similarity to the user's estimated preference for a reel to pass the OPTIONAL
    // topic-proximity filter (only consulted when the constructor's topicProximity flag is set).
    // A documented default, not config surface; 0.5 keeps reels within ~60 degrees of the estimate.
    static constexpr float kTopicProximityMinSimilarity = 0.5f;

    FreshCandidateSource(const std::vector<Reel> &reels, uint32_t count, double freshWindowSeconds,
                         bool topicProximity = false);

    std::vector<Candidate> generate(const User &user,
                                    const RecommendationRequest &request) override;

  private:
    struct Scored {
        Timestamp createdAt;
        uint32_t index; // index into reels_
    };

    const std::vector<Reel> &reels_;
    uint32_t count_;
    double freshWindowSeconds_;
    bool topicProximity_;
    std::vector<Scored> scratch_; // reused across generate() calls (single-threaded core, D13)
};

} // namespace rr
