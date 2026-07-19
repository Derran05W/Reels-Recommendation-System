#pragma once

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace rr {

// One optional [lo, hi] override for a single cohort-overridable trait. `present == false` (the
// default) means "leave the trait on its userTraitsV2 continuous range"; when present,
// augmentUsersV2 draws the trait uniformly in [lo, hi) instead of the default range. Every
// overridable trait lives in [0, 1], so a present override is validated 0 <= lo <= hi <= 1 at
// config load (D6/D10). The defaulted operator== makes two ABSENT overrides compare equal
// regardless of the (unused) lo/hi, so TraitCohortSpec's own defaulted operator== stays meaningful
// (an override-free spec equals another override-free spec of the same name/weight).
struct TraitOverride {
    double lo = 0.0;
    double hi = 1.0;
    bool present = false;
    // Absent overrides compare equal regardless of the (unused) lo/hi; present overrides compare by
    // bounds. Not defaulted so that TraitCohortSpec's defaulted operator== treats any two
    // override-free specs of the same name/weight as equal, and JSON round-trips stay stable
    // (to_json omits absent overrides, from_json restores them at the default bounds). Still an
    // aggregate (a user-declared comparison operator is not a constructor), so brace-init works.
    bool operator==(const TraitOverride &o) const {
        return present == o.present && (!present || (lo == o.lo && hi == o.hi));
    }
};

// One named trait-cohort profile (Phase 17, plan task 1): a mixture component over the Phase 13
// hidden tolerance/susceptibility traits, letting experiments generate populations pinned to (or
// mixed across) named behavioural cohorts — focused / novelty-seeker / creator-loyal /
// easily-fatigued — while the DEFAULT (empty mix) keeps Phase 13's continuous uniform trait
// sampling byte-identical (D17). Consumed by augmentUsersV2: when the mix is non-empty, each
// user first draws a cohort by weight on the "users-v2" stream, then samples any OVERRIDDEN
// trait within the cohort's [lo, hi] instead of the userTraitsV2 default range.
//
// PACKAGE-A OWNERSHIP, FROZEN CONTRACT: package A extends this struct with the per-trait
// override fields (all defaulted, covered by operator==) and implements the real serialization
// and validation. The struct name, `name`/`weight` members, default-constructibility,
// operator==, and the free-function declarations below must not change (config.{hpp,cpp} depends
// on them).
struct TraitCohortSpec {
    std::string name;
    double weight = 1.0; // relative mixture weight (> 0)

    // Per-trait optional overrides for the four augmentUsersV2 tolerance/appetite traits the
    // Phase 17 cohorts pin (all sampled in [0, 1] by userTraitsV2). An absent override (the
    // default) leaves that trait on its continuous range, so a spec with NO overrides samples a
    // user exactly like the default population — only the pinned traits change, never the draw
    // count (each trait is one uniform() draw regardless of range). JSON keys are snake_case:
    // repetition_tolerance / novelty_tolerance / creator_loyalty / novelty_seeking, each a
    // [lo, hi] pair. NOTE (plan task 1): preferenceConcentration is a V1 trait sampled in
    // generateUsers (user_generator.cpp), NOT augmentUsersV2, so it is deliberately NOT
    // overridable here — pinning it is out of Phase 17's scope.
    TraitOverride repetitionTolerance;
    TraitOverride noveltyTolerance;
    TraitOverride creatorLoyalty;
    TraitOverride noveltySeeking;

    bool operator==(const TraitCohortSpec &) const = default;
};

// D6: from_json rejects unknown keys, requires a non-empty name, validates weight > 0 and every
// override range (0 <= lo <= hi <= 1). Round-trip shape (absent overrides are omitted on
// serialization and round-trip back to absent):
//   {"name": "focused", "weight": 1.0, "repetition_tolerance": [0.8, 1.0],
//    "novelty_seeking": [0.0, 0.3]}
void to_json(nlohmann::json &j, const TraitCohortSpec &c);
void from_json(const nlohmann::json &j, TraitCohortSpec &c);

// The four Phase 17 experiment cohorts (plan task 1) as a single source of truth — the fatigue-
// heterogeneity statistical test pins populations to these, and package C transcribes their
// override ranges into configs/. Each returns a weight-1 spec pinning ONLY its designed traits;
// throws std::invalid_argument on an unknown name. Names (V2 TDD 4.10 core experiment):
//   "focused"         repetition_tolerance [0.8, 1.0], novelty_seeking     [0.0, 0.3]
//   "novelty_seeker"  novelty_seeking      [0.7, 1.0], repetition_tolerance [0.0, 0.3],
//                     novelty_tolerance    [0.7, 1.0]
//   "creator_loyal"   creator_loyalty      [0.8, 1.0]
//   "easily_fatigued" repetition_tolerance [0.0, 0.2], novelty_tolerance   [0.0, 0.3]
TraitCohortSpec namedCohort(const std::string &name);

} // namespace rr
