#include "rr/simulation/dataset_generator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/evaluation/cold_start.hpp"
#include "rr/infrastructure/config.hpp"

using rr::appendReels;
using rr::appendUsers;
using rr::Embedding;
using rr::generateDataset;
using rr::GeneratedDataset;
using rr::globalAveragePreference;
using rr::HiddenUserState;
using rr::isValid;
using rr::Reel;
using rr::SimulationConfig;
using rr::Timestamp;
using rr::User;

namespace {

// A small, non-degenerate simulation config for injection tests: enough topics/creators/reels/users
// that the generators exercise their full draw paths, but tiny enough to run instantly.
SimulationConfig makeConfig() {
    SimulationConfig c;
    c.seed = 4242;
    c.topics = 8;
    c.creators = 12;
    c.reels = 200;
    c.users = 60;
    c.dimensions = 32;
    return c;
}

// Field-wise equality of two reels (the fields the generator sets); floats compared bit-exactly
// because determinism is BYTE-identity, not approximate.
bool reelsIdentical(const Reel &a, const Reel &b) {
    return a.id.value == b.id.value && a.creatorId.value == b.creatorId.value &&
           a.embedding == b.embedding && a.intrinsicQuality == b.intrinsicQuality &&
           a.durationSeconds == b.durationSeconds && a.primaryTopic.value == b.primaryTopic.value &&
           a.secondaryTopics == b.secondaryTopics && a.createdAt == b.createdAt &&
           a.impressionCount == b.impressionCount && a.active == b.active;
}

bool hiddenIdentical(const HiddenUserState &a, const HiddenUserState &b) {
    return a.userId.value == b.userId.value && a.hiddenPreference == b.hiddenPreference &&
           a.preferredTopics == b.preferredTopics &&
           a.preferenceConcentration == b.preferenceConcentration &&
           a.exploreWillingness == b.exploreWillingness &&
           a.avgSessionLength == b.avgSessionLength && a.likePropensity == b.likePropensity &&
           a.sharePropensity == b.sharePropensity && a.durationTolerance == b.durationTolerance &&
           a.preferenceStability == b.preferenceStability;
}

} // namespace

// --- Determinism: same seed + count => byte-identical injected entities --------------------------

TEST(DatasetInjectionTest, AppendUsersDeterministic) {
    const SimulationConfig c = makeConfig();
    GeneratedDataset a = generateDataset(c, c.seed);
    GeneratedDataset b = generateDataset(c, c.seed);

    const std::size_t firstA = appendUsers(a, c, c.seed, 25);
    const std::size_t firstB = appendUsers(b, c, c.seed, 25);
    ASSERT_EQ(firstA, firstB);
    ASSERT_EQ(a.users.size(), b.users.size());
    ASSERT_EQ(a.hiddenStates.size(), b.hiddenStates.size());
    for (std::size_t i = firstA; i < a.users.size(); ++i) {
        EXPECT_EQ(a.users[i].id.value, b.users[i].id.value);
        EXPECT_TRUE(hiddenIdentical(a.hiddenStates[i], b.hiddenStates[i]))
            << "injected user " << i << " differs between two same-seed appends";
    }
}

TEST(DatasetInjectionTest, AppendReelsDeterministic) {
    const SimulationConfig c = makeConfig();
    GeneratedDataset a = generateDataset(c, c.seed);
    GeneratedDataset b = generateDataset(c, c.seed);

    const Timestamp injectAt = 123456;
    const std::size_t firstA = appendReels(a, c, c.seed, 40, injectAt);
    const std::size_t firstB = appendReels(b, c, c.seed, 40, injectAt);
    ASSERT_EQ(firstA, firstB);
    ASSERT_EQ(a.reels.size(), b.reels.size());
    for (std::size_t i = firstA; i < a.reels.size(); ++i) {
        EXPECT_TRUE(reelsIdentical(a.reels[i], b.reels[i]))
            << "injected reel " << i << " differs between two same-seed appends";
    }
}

// --- Dense-id invariant preserved after append (the factory relies on it) ------------------------

TEST(DatasetInjectionTest, AppendPreservesDenseIds) {
    const SimulationConfig c = makeConfig();
    GeneratedDataset ds = generateDataset(c, c.seed);

    const std::size_t firstReel = appendReels(ds, c, c.seed, 40, 999);
    const std::size_t firstUser = appendUsers(ds, c, c.seed, 25);
    EXPECT_EQ(firstReel, static_cast<std::size_t>(c.reels));
    EXPECT_EQ(firstUser, static_cast<std::size_t>(c.users));

    for (std::size_t i = 0; i < ds.reels.size(); ++i) {
        EXPECT_EQ(ds.reels[i].id.value, i) << "reel id not dense at " << i;
    }
    for (std::size_t i = 0; i < ds.users.size(); ++i) {
        EXPECT_EQ(ds.users[i].id.value, i) << "user id not dense at " << i;
        EXPECT_EQ(ds.hiddenStates[i].userId.value, i);
    }
}

// --- The D8 property: enabling injection leaves the ORIGINAL dataset byte-identical --------------

TEST(DatasetInjectionTest, OriginalDatasetByteIdenticalWithAndWithoutInjection) {
    const SimulationConfig c = makeConfig();
    GeneratedDataset plain = generateDataset(c, c.seed); // never appended
    GeneratedDataset grown = generateDataset(c, c.seed); // appended below

    appendReels(grown, c, c.seed, 40, 55555);
    appendUsers(grown, c, c.seed, 25);

    // Original reels/users/hidden states are untouched by the appends (independent rng streams).
    ASSERT_GE(grown.reels.size(), plain.reels.size());
    for (std::size_t i = 0; i < plain.reels.size(); ++i) {
        EXPECT_TRUE(reelsIdentical(grown.reels[i], plain.reels[i]))
            << "original reel " << i << " perturbed by injection";
    }
    ASSERT_GE(grown.users.size(), plain.users.size());
    for (std::size_t i = 0; i < plain.users.size(); ++i) {
        EXPECT_EQ(grown.users[i].id.value, plain.users[i].id.value);
        EXPECT_TRUE(hiddenIdentical(grown.hiddenStates[i], plain.hiddenStates[i]))
            << "original hidden state " << i << " perturbed by injection";
    }
    // Topics/creators are shared, never regenerated.
    EXPECT_EQ(grown.topics.size(), plain.topics.size());
    EXPECT_EQ(grown.creators.size(), plain.creators.size());
}

// --- Injected reels are the genuinely-fresh content ---------------------------------------------

TEST(DatasetInjectionTest, InjectedReelsCarryCreatedAtZeroCountersActive) {
    const SimulationConfig c = makeConfig();
    GeneratedDataset ds = generateDataset(c, c.seed);
    const Timestamp injectAt = 7'200'000; // a distinct, non-window value
    const std::size_t first = appendReels(ds, c, c.seed, 40, injectAt);

    for (std::size_t i = first; i < ds.reels.size(); ++i) {
        const Reel &r = ds.reels[i];
        EXPECT_EQ(r.createdAt, injectAt) << "injected reel " << i << " has wrong createdAt";
        EXPECT_EQ(r.impressionCount, 0u);
        EXPECT_EQ(r.completionCount, 0u);
        EXPECT_EQ(r.likeCount, 0u);
        EXPECT_EQ(r.shareCount, 0u);
        EXPECT_EQ(r.skipCount, 0u);
        EXPECT_DOUBLE_EQ(r.trendingEngagement, 0.0);
        EXPECT_DOUBLE_EQ(r.trendingImpressions, 0.0);
        EXPECT_TRUE(r.active);
        EXPECT_TRUE(isValid(r.embedding)) << "injected reel " << i << " embedding not unit-length";
        EXPECT_LT(r.creatorId.value, c.creators);  // built over the EXISTING creators
        EXPECT_LT(r.primaryTopic.value, c.topics); // over the EXISTING topics
    }
}

// --- Injected users: frozen cold-start prior, unit vectors, D11 separation intact ----------------

TEST(DatasetInjectionTest, InjectedUsersCarryFrozenPriorUnitVectorsSeparation) {
    const SimulationConfig c = makeConfig();
    GeneratedDataset ds = generateDataset(c, c.seed);

    // The run-start prior is frozen from the ORIGINAL population's hidden states (TDD 11.1).
    const Embedding prior = globalAveragePreference(ds.hiddenStates);
    ASSERT_TRUE(isValid(prior));

    const std::size_t first = appendUsers(ds, c, c.seed, 25);
    // The harness applies the frozen prior to the injected slice; replicate that here.
    for (std::size_t i = first; i < ds.users.size(); ++i) {
        ds.users[i].estimatedPreference = prior;
        ds.users[i].longTermPreference = prior;
        ds.users[i].sessionPreference = prior;
    }

    for (std::size_t i = first; i < ds.users.size(); ++i) {
        const User &u = ds.users[i];
        // Recommender-visible vectors are the frozen prior, unit-length.
        EXPECT_EQ(u.estimatedPreference, prior);
        EXPECT_EQ(u.longTermPreference, prior);
        EXPECT_EQ(u.sessionPreference, prior);
        EXPECT_TRUE(isValid(u.estimatedPreference));

        // Hidden state exists, is aligned, and is a real unit-length preference DISTINCT from the
        // recommender-visible prior (D11: the recommender never sees this vector).
        const HiddenUserState &h = ds.hiddenStates[i];
        EXPECT_EQ(h.userId.value, u.id.value);
        EXPECT_TRUE(isValid(h.hiddenPreference));
        EXPECT_GE(h.preferredTopics.size(), 2u);
        EXPECT_LE(h.preferredTopics.size(), 5u);
        EXPECT_NE(h.hiddenPreference, u.estimatedPreference);
    }
}

// --- Zero-count appends are no-ops ---------------------------------------------------------------

TEST(DatasetInjectionTest, ZeroCountAppendIsNoOp) {
    const SimulationConfig c = makeConfig();
    GeneratedDataset ds = generateDataset(c, c.seed);
    const std::size_t reelsBefore = ds.reels.size();
    const std::size_t usersBefore = ds.users.size();

    EXPECT_EQ(appendReels(ds, c, c.seed, 0, 123), reelsBefore);
    EXPECT_EQ(appendUsers(ds, c, c.seed, 0), usersBefore);
    EXPECT_EQ(ds.reels.size(), reelsBefore);
    EXPECT_EQ(ds.users.size(), usersBefore);
    EXPECT_EQ(ds.hiddenStates.size(), usersBefore);
}
