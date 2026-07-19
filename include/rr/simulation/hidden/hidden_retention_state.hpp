#pragma once

#include <cstdint>

#include "rr/infrastructure/clock.hpp"

namespace rr {

// Hidden long-term retention state (V2 TDD §4.17, Phase 20), owned SOLELY by the simulator (D18):
// the persistent trust / habit a user carries BETWEEN sessions, driving how soon — or whether —
// they return. Default-initialized on every HiddenUserState; gate-off runs never read or write it,
// so a run with retention.enabled off is byte-identical to a pre-Phase-20 run (D17).
//
// FROZEN CROSS-PACKAGE SURFACE (contracts §2): the field list below is the exact shared shape both
// P20 packages agreed on before launch — do NOT add / rename / remove a field without an
// orchestrator sign-off (the P19 lesson: a guessed field name costs an integration session).
//
// WRITE DISCIPLINE (frozen): Package A's PreferenceEvolution component is the ONLY writer of
// `trust` (satisfaction/regret-weighted erosion and slow recovery, clamped [0,1], initializing from
// hidden.platformTrust on first touch). Package B's RetentionModel READS `trust` and owns every
// OTHER field (habitStrength, churned, the last-session memory, lastExitAt, sessionsCompleted).
struct HiddenRetentionState {
    double trust = -1.0;        // accumulated platform trust in [0,1]; -1 = uninitialized
                                // (initialized to hidden.platformTrust on first gate-on use)
    double habitStrength = 0.0; // [0,1], strengthens with satisfying sessions, decays away
    bool churned = false;       // set by RetentionModel when delay > churn threshold
    double lastSessionSatisfaction = 0.0;
    double lastSessionRegret = 0.0;
    Timestamp lastExitAt = 0;
    uint32_t sessionsCompleted = 0;
};

} // namespace rr
