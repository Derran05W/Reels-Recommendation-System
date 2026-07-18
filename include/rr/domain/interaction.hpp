#pragma once

#include <cstdint>

#include "rr/domain/candidate.hpp"
#include "rr/domain/ids.hpp"
#include "rr/infrastructure/clock.hpp"

namespace rr {

// TDD 8.4.
enum class InteractionType {
    Impression,
    InstantSkip,
    PartialWatch,
    CompleteWatch,
    Rewatch,
    Like,
    Share,
    FollowCreator,
    NotInterested
};

struct InteractionEvent {
    UserId userId;
    ReelId reelId;
    CreatorId creatorId;

    InteractionType type;

    float watchSeconds;
    float watchRatio;
    float reward;

    Timestamp timestamp;
    SessionId sessionId;

    // --- Realism V2 observable event fields (V2 TDD 5, Phase 14) -----------------------------
    // Populated by the Simulator's V2 path only when realism.latent_reactions is on; the
    // defaults below are the gate-off values (V1 event assembly never writes them, D17). ALL
    // fields here are observables — no latent value ever rides the event (leak-audit enforced;
    // extend the serialization allowlist in the SAME commit as any new field).
    uint32_t positionInFeed = 0;    // 0-based slot of the reel in the served feed
    uint64_t requestId = 0;         // feed/request identifier (harness-global counter)
    Timestamp requestTimestamp = 0; // logical time the feed request was served
    Timestamp startTimestamp = 0;   // logical time playback started
    Timestamp finishTimestamp = 0;  // logical time playback ended
    float dwellSeconds = 0.0f;      // time on the impression (watch + documented overhead)
    uint32_t replayCount = 0;       // whole extra plays beyond the first
    bool commented = false;         // V2 engagement signals (TDD 4.3)
    bool saved = false;
    bool profileVisited = false;
    bool fromExploration = false; // impression came from an exploration slot (TDD 5)
    // Candidate-source provenance of the served item (the feed item's elected representative
    // source, Phase 8 semantics). VectorHNSW is the benign default for gate-off events.
    CandidateSource sourceProvenance = CandidateSource::VectorHNSW;
    // Placeholder (V2 TDD 5): set on the LAST event of a session once probabilistic exit lands
    // in Phase 16; stays false until then.
    bool observedExitAfterImpression = false;
};

} // namespace rr
