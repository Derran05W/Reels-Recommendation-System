#pragma once

#include <memory>
#include <vector>

#include "rr/domain/creator.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/recommender.hpp"
#include "rr/simulation/hidden/hidden_reel_state.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"

namespace rr {

// The Phase 15 evaluation-only oracle arm (V2 TDD 4.4 core experiment, arm 4): HNSW retrieval
// over the SAME semantic candidate pool as the hnsw arm, then ranked by the user's EXPECTED
// hidden satisfaction — the noise-free deterministic part of rr::computeLatentReaction (a
// zero-noise BehaviourV2Config copy; the mandatory satisfaction-stream draw goes to a throwaway
// rng). It upper-bounds what any observable-only policy could achieve on satisfaction.
//
// D18: this class reads HiddenUserState/HiddenReelState, so it lives in evaluation/, is named
// oracle*, is constructed ONLY by the ExperimentRunner (the recommendation-side factory throws
// for RecommendationAlgorithm::OracleSatisfaction), and is BARRED from being a trainable or
// serving policy. It is an experiment ceiling, nothing else.
//
// PACKAGE-B2 OWNERSHIP, FROZEN SIGNATURES: package B2 implements this class (currently a stub
// that throws); the constructor and Recommender overrides must not change (the runner calls
// them).
class OracleSatisfactionRecommender final : public Recommender {
  public:
    OracleSatisfactionRecommender(const ExperimentConfig &config, const std::vector<Reel> &reels,
                                  const std::vector<User> &users,
                                  const std::vector<Creator> &creators,
                                  const std::vector<HiddenUserState> &hiddenStates,
                                  const std::vector<HiddenReelState> &hiddenReelStates, Rng rng);
    ~OracleSatisfactionRecommender() override;

    RecommendationResponse recommend(const RecommendationRequest &request) override;
    std::string name() const override;
    const VectorIndex *retrievalIndex() const override;
    void onReelsAppended(size_t firstNewIndex) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rr
