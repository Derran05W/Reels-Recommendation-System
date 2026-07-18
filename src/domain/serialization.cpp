#include "rr/domain/serialization.hpp"

#include <nlohmann/json.hpp>

namespace rr {

namespace {

// Local InteractionType -> string mapping (D18 leak-audit scope: this is the ONLY place the
// nine InteractionType enumerators get a serialized spelling, snake_case per D6/D10, matching
// the existing rr::toString(RecommendationAlgorithm) convention in config.cpp). The switch is
// exhaustive over all nine enumerators; the trailing return (never reached) exists only to keep
// -Wall/-Wextra/-Werror quiet about a possible fall-through past the switch, same idiom as
// toString(RecommendationAlgorithm).
const char *interactionTypeToString(InteractionType type) {
    switch (type) {
    case InteractionType::Impression:
        return "impression";
    case InteractionType::InstantSkip:
        return "instant_skip";
    case InteractionType::PartialWatch:
        return "partial_watch";
    case InteractionType::CompleteWatch:
        return "complete_watch";
    case InteractionType::Rewatch:
        return "rewatch";
    case InteractionType::Like:
        return "like";
    case InteractionType::Share:
        return "share";
    case InteractionType::FollowCreator:
        return "follow_creator";
    case InteractionType::NotInterested:
        return "not_interested";
    }
    return "impression";
}

// Candidate-source provenance -> string (V2 TDD 5, Phase 14). The elected representative source of
// the served feed item rides on the InteractionEvent as an OBSERVABLE (it is a serving-time fact,
// not hidden state); this is the only place the enumerator gets a serialized snake_case spelling.
// Exhaustive over the seven CandidateSource enumerators; the trailing return is unreachable and
// only keeps -Werror quiet, same idiom as interactionTypeToString.
const char *candidateSourceToString(CandidateSource source) {
    switch (source) {
    case CandidateSource::VectorHNSW:
        return "vector_hnsw";
    case CandidateSource::VectorExact:
        return "vector_exact";
    case CandidateSource::Popular:
        return "popular";
    case CandidateSource::Trending:
        return "trending";
    case CandidateSource::Fresh:
        return "fresh";
    case CandidateSource::CreatorAffinity:
        return "creator_affinity";
    case CandidateSource::Exploration:
        return "exploration";
    }
    return "vector_hnsw";
}

} // namespace

// D18 leak-audit schema for InteractionEvent. The nine V1 members plus the thirteen Realism V2
// observable fields (V2 TDD 5, Phase 14: feed position, request id/timestamp, playback
// start/finish/dwell, replay count, comment/save/profile-visit signals, exploration flag,
// candidate-source provenance, and the P16 exit placeholder) — every one an OBSERVABLE serving- or
// interaction-time fact. tests/unit/leak_audit_test.cpp asserts this exact key set; ANY field
// added to InteractionEvent must be a conscious addition here AND to that test's allowlist in the
// SAME commit. Hidden/latent fields (satisfaction, regret, archetype, fatigue, ...) must NEVER
// appear (V2 TDD S5: "Do not include hidden satisfaction directly") — the latent LatentReaction
// reaches only the welfare metrics through the D18 evaluation carve-out, never this struct.
void to_json(nlohmann::json &j, const InteractionEvent &e) {
    j = nlohmann::json{
        {"user_id", e.userId.value},
        {"reel_id", e.reelId.value},
        {"creator_id", e.creatorId.value},
        {"type", interactionTypeToString(e.type)},
        {"watch_seconds", e.watchSeconds},
        {"watch_ratio", e.watchRatio},
        {"reward", e.reward},
        {"timestamp", e.timestamp},
        {"session_id", e.sessionId.value},
        // --- Realism V2 observable fields (V2 TDD 5, Phase 14) ---
        {"position_in_feed", e.positionInFeed},
        {"request_id", e.requestId},
        {"request_timestamp", e.requestTimestamp},
        {"start_timestamp", e.startTimestamp},
        {"finish_timestamp", e.finishTimestamp},
        {"dwell_seconds", e.dwellSeconds},
        {"replay_count", e.replayCount},
        {"commented", e.commented},
        {"saved", e.saved},
        {"profile_visited", e.profileVisited},
        {"from_exploration", e.fromExploration},
        {"source_provenance", candidateSourceToString(e.sourceProvenance)},
        {"observed_exit_after_impression", e.observedExitAfterImpression},
    };
}

// D18 leak-audit schema for User (the recommender-visible profile — D11 already keeps hidden
// preference off this struct entirely). seen_reels / creator_affinity / recent_interactions are
// emitted as COUNTS, not their contents: the audit cares about which KEYS are observable, and
// serializing (say) a 100k-entry seen-reel set would be both useless for the audit and enormous
// in any training log that reuses this function (D18: "the P22 training log MUST serialize
// events through this function"). Extend both this function and the allowlist test together if a
// field is ever added to User.
void to_json(nlohmann::json &j, const User &u) {
    j = nlohmann::json{
        {"id", u.id.value},
        {"estimated_preference", u.estimatedPreference},
        {"long_term_preference", u.longTermPreference},
        {"session_preference", u.sessionPreference},
        {"seen_reels", u.seenReels.size()},
        {"creator_affinity", u.creatorAffinity.size()},
        {"recent_interactions", u.recentInteractions.size()},
        {"total_interactions", u.totalInteractions},
        {"current_session_length", u.currentSessionLength},
    };
}

} // namespace rr
