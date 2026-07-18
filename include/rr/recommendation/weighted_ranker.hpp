#pragma once

#include <vector>

#include "rr/domain/candidate.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/clock.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/recommendation/feature_extractor.hpp"
#include "rr/recommendation/ranker.hpp"

namespace rr {

// TDD 14.2 / 14.4: the deterministic second-stage ranker. Scores each candidate by the weighted
// sum of its normalized features (weights from RankingConfig),
//
//   score = wS*S + wST*ST + wQ*Q + wF*F + wP*P + wT*T + wC*C + wE*E + wD*D - wR*R - wI*I
//
// where ST is the session-topic similarity (TDD 14.1), a positive contribution activated in
// Phase 7 alongside online session-vector updates (wST == session_topic_weight). Returns the
// candidates sorted by rankingScore DESCENDING, ties broken by ascending ReelId (a total order, so
// the output is fully deterministic - same doctrine as the Orchestrator).
//
// Every returned candidate carries its rankingScore AND a featureContributions map. The ELEVEN
// FROZEN V1 snake_case keys are ALWAYS present (penalties stored as NEGATIVE values): similarity,
// session_topic, quality, freshness, popularity, trending, creator_affinity, exploration,
// duration_match, repetition_penalty, impression_penalty.
//
// Under `contentV2` (Phase 15) the map ADDITIONALLY carries the TEN gated V2 keys — visual_match,
// music_match, emotional_match, clickbait, emotional_intensity, usefulness, production_quality,
// information_density, language_match, save_popularity — each stored as weight * feature (the sign
// comes from the config weight, so a NEGATIVE preset weight, e.g. the satisfaction-proxy arm
// penalizing clickbait, yields a negative contribution). With contentV2 false (the default) NONE of
// the V2 keys is emitted and the map is byte-identical to the pre-Phase-15 ranker (D17).
//
// The map's values sum to rankingScore to float tolerance (property-tested), whichever key set is
// present.
class WeightedRanker final : public Ranker {
  public:
    // `contentV2` (Phase 15): forwarded to the FeatureExtractor; when true, package B1's V2
    // feature contributions are emitted (gated keys), when false (default) output is
    // byte-identical to the pre-Phase-15 ranker (D17).
    WeightedRanker(const std::vector<Reel> &reels, const RankingConfig &config,
                   bool contentV2 = false);

    std::vector<Candidate> rank(const User &user, const std::vector<Candidate> &candidates,
                                Timestamp now) const override;

  private:
    RankingConfig config_;
    FeatureExtractor extractor_;
    // Phase 15: gates the ten V2 contribution keys in rank(); false keeps the map byte-identical to
    // the pre-Phase-15 ranker (D17). Also forwarded to the FeatureExtractor at construction.
    bool contentV2_ = false;
};

} // namespace rr
