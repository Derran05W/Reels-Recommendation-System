// Phase 17 (package A, plan task 1) — TraitCohortSpec extension unit tests: per-trait [lo, hi]
// override serialization/validation (D6 unknown-key rejection, non-empty name, weight > 0,
// 0 <= lo <= hi <= 1), the defaulted operator== over the overrides, JSON round-trip, and the four
// named experiment cohorts (the single source of truth package C transcribes into configs/).

#include "rr/infrastructure/cohort_config.hpp"

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using nlohmann::json;
using rr::namedCohort;
using rr::TraitCohortSpec;
using rr::TraitOverride;

namespace {

// A JSON cohort object with a name/weight plus arbitrary extra keys, for the parse tests.
json cohortJson(const std::string &name, double weight) {
    return json{{"name", name}, {"weight", weight}};
}

} // namespace

// --- Defaults + operator== ------------------------------------------------------------------

TEST(CohortConfigTest, DefaultSpecHasNoOverrides) {
    TraitCohortSpec c;
    EXPECT_DOUBLE_EQ(c.weight, 1.0);
    EXPECT_FALSE(c.repetitionTolerance.present);
    EXPECT_FALSE(c.noveltyTolerance.present);
    EXPECT_FALSE(c.creatorLoyalty.present);
    EXPECT_FALSE(c.noveltySeeking.present);
}

TEST(CohortConfigTest, OperatorEqualsCoversOverridesButIgnoresAbsentBounds) {
    TraitCohortSpec a;
    a.name = "x";
    TraitCohortSpec b;
    b.name = "x";
    // Two absent overrides compare equal even with different (unused) lo/hi bounds.
    a.repetitionTolerance = TraitOverride{0.1, 0.2, false};
    b.repetitionTolerance = TraitOverride{0.7, 0.9, false};
    EXPECT_EQ(a, b);
    // A present override that differs makes the specs unequal.
    b.repetitionTolerance = TraitOverride{0.7, 0.9, true};
    EXPECT_NE(a, b);
    a.repetitionTolerance = TraitOverride{0.7, 0.9, true};
    EXPECT_EQ(a, b);
}

// --- from_json validation -------------------------------------------------------------------

TEST(CohortConfigTest, ParsesNameWeightAndOverrides) {
    json j = cohortJson("focused", 2.0);
    j["repetition_tolerance"] = json::array({0.8, 1.0});
    j["novelty_seeking"] = json::array({0.0, 0.3});
    auto c = j.get<TraitCohortSpec>();
    EXPECT_EQ(c.name, "focused");
    EXPECT_DOUBLE_EQ(c.weight, 2.0);
    ASSERT_TRUE(c.repetitionTolerance.present);
    EXPECT_DOUBLE_EQ(c.repetitionTolerance.lo, 0.8);
    EXPECT_DOUBLE_EQ(c.repetitionTolerance.hi, 1.0);
    ASSERT_TRUE(c.noveltySeeking.present);
    EXPECT_DOUBLE_EQ(c.noveltySeeking.lo, 0.0);
    EXPECT_DOUBLE_EQ(c.noveltySeeking.hi, 0.3);
    // Unspecified overrides stay absent.
    EXPECT_FALSE(c.noveltyTolerance.present);
    EXPECT_FALSE(c.creatorLoyalty.present);
}

TEST(CohortConfigTest, WeightDefaultsToOneWhenOmitted) {
    json j = {{"name", "c"}};
    auto c = j.get<TraitCohortSpec>();
    EXPECT_DOUBLE_EQ(c.weight, 1.0);
}

TEST(CohortConfigTest, RejectsUnknownKey) {
    json j = cohortJson("c", 1.0);
    j["repetition_toleranse"] = json::array({0.0, 1.0}); // typo
    EXPECT_THROW(j.get<TraitCohortSpec>(), std::invalid_argument);
}

TEST(CohortConfigTest, RejectsEmptyOrMissingName) {
    EXPECT_THROW(cohortJson("", 1.0).get<TraitCohortSpec>(), std::invalid_argument);
    EXPECT_THROW((json{{"weight", 1.0}}).get<TraitCohortSpec>(), std::invalid_argument);
}

TEST(CohortConfigTest, RejectsNonPositiveWeight) {
    EXPECT_THROW(cohortJson("c", 0.0).get<TraitCohortSpec>(), std::invalid_argument);
    EXPECT_THROW(cohortJson("c", -1.0).get<TraitCohortSpec>(), std::invalid_argument);
}

TEST(CohortConfigTest, RejectsBadOverrideRanges) {
    const auto bad = [](const json &range) {
        json j = cohortJson("c", 1.0);
        j["creator_loyalty"] = range;
        return j;
    };
    EXPECT_THROW(bad(json::array({0.6, 0.4})).get<TraitCohortSpec>(),
                 std::invalid_argument); // lo > hi
    EXPECT_THROW(bad(json::array({-0.1, 0.5})).get<TraitCohortSpec>(),
                 std::invalid_argument); // lo < 0
    EXPECT_THROW(bad(json::array({0.5, 1.1})).get<TraitCohortSpec>(),
                 std::invalid_argument); // hi > 1
    EXPECT_THROW(bad(json::array({0.5})).get<TraitCohortSpec>(),
                 std::invalid_argument); // not a pair
    EXPECT_THROW(bad(json::array({0.0, 0.5, 1.0})).get<TraitCohortSpec>(),
                 std::invalid_argument); // three elements
    EXPECT_THROW(bad(json("nope")).get<TraitCohortSpec>(),
                 std::invalid_argument); // not an array
    // Degenerate but valid: lo == hi (a pinned point) is accepted.
    EXPECT_NO_THROW(bad(json::array({0.5, 0.5})).get<TraitCohortSpec>());
}

// --- Round-trip -----------------------------------------------------------------------------

TEST(CohortConfigTest, RoundTripsWithAndWithoutOverrides) {
    // No overrides: to_json emits only name/weight, round-trips to an override-free spec.
    TraitCohortSpec bare;
    bare.name = "bare";
    bare.weight = 1.5;
    const json bareJson = bare;
    EXPECT_FALSE(bareJson.contains("repetition_tolerance"));
    EXPECT_EQ(bareJson.get<TraitCohortSpec>(), bare);

    // With overrides: present overrides serialize as [lo, hi] arrays and round-trip.
    TraitCohortSpec full = namedCohort("novelty_seeker");
    const json fullJson = full;
    EXPECT_TRUE(fullJson.contains("novelty_seeking"));
    EXPECT_TRUE(fullJson.contains("repetition_tolerance"));
    EXPECT_TRUE(fullJson.contains("novelty_tolerance"));
    EXPECT_FALSE(fullJson.contains("creator_loyalty"));
    EXPECT_EQ(fullJson.get<TraitCohortSpec>(), full);
}

// --- Named cohorts (the single source of truth) --------------------------------------------

TEST(CohortConfigTest, NamedCohortsPinTheDesignedTraitRanges) {
    const TraitCohortSpec focused = namedCohort("focused");
    EXPECT_EQ(focused.name, "focused");
    EXPECT_DOUBLE_EQ(focused.weight, 1.0);
    ASSERT_TRUE(focused.repetitionTolerance.present);
    EXPECT_DOUBLE_EQ(focused.repetitionTolerance.lo, 0.8);
    EXPECT_DOUBLE_EQ(focused.repetitionTolerance.hi, 1.0);
    ASSERT_TRUE(focused.noveltySeeking.present);
    EXPECT_DOUBLE_EQ(focused.noveltySeeking.hi, 0.3);
    EXPECT_FALSE(focused.creatorLoyalty.present);

    const TraitCohortSpec seeker = namedCohort("novelty_seeker");
    ASSERT_TRUE(seeker.noveltySeeking.present);
    EXPECT_DOUBLE_EQ(seeker.noveltySeeking.lo, 0.7);
    ASSERT_TRUE(seeker.repetitionTolerance.present);
    EXPECT_DOUBLE_EQ(seeker.repetitionTolerance.hi, 0.3);
    ASSERT_TRUE(seeker.noveltyTolerance.present);
    EXPECT_DOUBLE_EQ(seeker.noveltyTolerance.lo, 0.7);

    const TraitCohortSpec loyal = namedCohort("creator_loyal");
    ASSERT_TRUE(loyal.creatorLoyalty.present);
    EXPECT_DOUBLE_EQ(loyal.creatorLoyalty.lo, 0.8);
    EXPECT_DOUBLE_EQ(loyal.creatorLoyalty.hi, 1.0);
    EXPECT_FALSE(loyal.repetitionTolerance.present);

    const TraitCohortSpec ef = namedCohort("easily_fatigued");
    ASSERT_TRUE(ef.repetitionTolerance.present);
    EXPECT_DOUBLE_EQ(ef.repetitionTolerance.hi, 0.2);
    ASSERT_TRUE(ef.noveltyTolerance.present);
    EXPECT_DOUBLE_EQ(ef.noveltyTolerance.hi, 0.3);
}

TEST(CohortConfigTest, NamedCohortsAreValidAndSerializable) {
    // Each named cohort must survive the same from_json validation package C's configs will hit.
    for (const char *name : {"focused", "novelty_seeker", "creator_loyal", "easily_fatigued"}) {
        const TraitCohortSpec c = namedCohort(name);
        const json j = c;
        EXPECT_NO_THROW(j.get<TraitCohortSpec>()) << name;
        EXPECT_EQ(j.get<TraitCohortSpec>(), c) << name;
    }
}

TEST(CohortConfigTest, NamedCohortRejectsUnknownName) {
    EXPECT_THROW(namedCohort("wanderer"), std::invalid_argument);
}
