#pragma once

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/interaction.hpp"

namespace rr {

// TDD 8.3, minus hiddenPreference (design decision D11): the recommender-visible User carries no
// hidden state. The simulator's ground-truth preference lives in HiddenUserState instead.
struct User {
    UserId id;

    Embedding estimatedPreference;
    Embedding longTermPreference;
    Embedding sessionPreference;

    // --- Realism V2 recommender-visible modality estimates (V2 TDD 5, Phase 15) --------------
    // Per-modality EMA estimates maintained by OnlineUserStateUpdater from OBSERVABLE reward
    // only (mirroring the V1 11.2 rule, applied to the reel's modality embeddings), and only
    // when realism.content_v2 is on — gate-off leaves them empty (D17). They feed the V2
    // ranking features (modality-match); the candidate QUERY stays semantic-only (D23). These
    // are estimates, never hidden truth (D11/D18); the serialization leak-audit allowlist
    // covers them explicitly.
    Embedding estimatedVisualPreference{};
    Embedding estimatedMusicPreference{};
    Embedding estimatedEmotionalPreference{};

    // --- Phase 17 recommender-visible tolerance estimates (V2 TDD 4.10/5) --------------------
    // Maintained by ToleranceEstimator from OBSERVABLE signals only (declining within-topic
    // completion runs, not-interested after repeats, exit-after-repetition, comment/save
    // cadence) and only under realism.personalized_diversity; gate-off leaves the neutral
    // defaults (D17). Consumed by PersonalizedDiversityReranker (per-user caps / MMR lambda /
    // repetition scaling). Estimates, never hidden truth (D11/D18); serialization allowlist
    // covers them explicitly.
    float estimatedRepetitionTolerance = 0.5F; // 0.5 = neutral (no evidence yet)
    float estimatedNoveltyTolerance = 0.5F;
    std::unordered_map<TopicId, float> estimatedTopicFatigue{};
    std::unordered_map<CreatorId, float> estimatedCreatorFatigue{};

    std::unordered_set<ReelId> seenReels;
    std::unordered_map<CreatorId, float> creatorAffinity;

    std::deque<InteractionEvent> recentInteractions;

    uint64_t totalInteractions;
    uint64_t currentSessionLength;
};

} // namespace rr
