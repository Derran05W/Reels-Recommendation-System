#pragma once

#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"

namespace rr {

// Ground-truth user state owned SOLELY by the simulator (design decision D11). The recommender
// must never see this type — the "recommender never accesses hidden preference" property is thus
// a structural, compile-time guarantee. Behavioural traits (TDD 9.3) are sampled per user in
// Phase 2 and consumed by the behaviour model in Phase 3.
struct HiddenUserState {
    UserId userId;

    // True latent preference vector (L2-normalized). The simulator scores reels against this; the
    // recommender never sees it.
    Embedding hiddenPreference;

    // Ground-truth topics (2-5, distinct) whose weighted mix formed hiddenPreference. Recorded so
    // the simulator/tests can reason about the user's true interests. Not visible to the
    // recommender.
    std::vector<TopicId> preferredTopics{};

    // --- Per-user behavioural traits (TDD 9.3 user-variation axes). Each field documents what it
    //     modulates and its valid sampling range; generateUsers samples within these ranges and
    //     the tests verify every generated value falls inside them. ---

    // Preference concentration: peakedness of the topic weights that formed hiddenPreference.
    // Higher => one topic dominates; lower => a flatter mix. Valid range [0.5, 4.0].
    float preferenceConcentration = 1.0f;

    // Willingness to explore unfamiliar / off-preference content (probability-like). [0.0, 1.0].
    float exploreWillingness = 0.0f;

    // Average session length: mean number of reels the user watches per session. [5.0, 40.0].
    float avgSessionLength = 0.0f;

    // Baseline propensity to like a reel, before affinity/quality modulation. [0.02, 0.25].
    float likePropensity = 0.0f;

    // Baseline propensity to share a reel, before affinity/quality modulation. [0.0, 0.10].
    float sharePropensity = 0.0f;

    // Tolerance for long videos: higher => more willing to keep watching long durations.
    // [0.0, 1.0].
    float durationTolerance = 0.0f;

    // Preference stability: how slowly the hidden preference drifts over time (higher => more
    // stable). Consumed by preference-drift logic in later phases. [0.0, 1.0].
    float preferenceStability = 0.0f;
};

} // namespace rr
