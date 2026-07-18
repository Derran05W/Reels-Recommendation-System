#include "rr/learning/online_user_state_updater.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/interaction.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"

// Unit tests for the Realism V2 gated per-modality EMA in rr::OnlineUserStateUpdater (Phase 15,
// plan task 2 / V2 TDD 5). Every value is hand-computed to float tolerance. The estimate update
// mirrors the V1 11.2 long-term rule applied to the reel's modality embeddings, driven by the SAME
// observable reward, and is performed ONLY when the updater was constructed with contentV2 = true
// (the realism.content_v2 gate). Gate-off (the default) must leave the three estimate vectors
// exactly as they were and never change the V1 preference vectors.

using namespace rr;

namespace {

constexpr float kTol = 1e-4f;

// A reel carrying the V1 embedding the V1 rules read plus the three V2 modality embeddings the
// gated modality EMA reads. Any argument left {} is an absent (gate-off default) modality
// embedding.
Reel makeReel(uint32_t id, Embedding semantic, Embedding visual = {}, Embedding music = {},
              Embedding emotional = {}) {
    Reel r{};
    r.id = ReelId{id};
    r.creatorId = CreatorId{0};
    r.embedding = std::move(semantic);
    r.visualStyleEmbedding = std::move(visual);
    r.musicEmbedding = std::move(music);
    r.emotionalToneEmbedding = std::move(emotional);
    return r;
}

InteractionEvent makeEvent(uint32_t reelId, float reward, uint32_t sessionId = 7) {
    InteractionEvent e{};
    e.userId = UserId{0};
    e.reelId = ReelId{reelId};
    e.creatorId = CreatorId{0};
    e.type = InteractionType::CompleteWatch;
    e.reward = reward;
    e.timestamp = 0;
    e.sessionId = SessionId{sessionId};
    return e;
}

// A cold-started user: the three V1 preference vectors set to `prior` (a unit vector), the three V2
// modality estimates left EMPTY (their gate-off / pre-first-observation default).
User makeUser(const Embedding &prior) {
    User u{};
    u.id = UserId{0};
    u.estimatedPreference = prior;
    u.longTermPreference = prior;
    u.sessionPreference = prior;
    return u;
}

// Drive one apply() with `interaction` already appended to the recent window (the call-site
// contract), returning the mutated user.
User applyOne(const std::vector<Reel> &reels, User user, const InteractionEvent &e,
              const LearningConfig &cfg, bool contentV2) {
    OnlineUserStateUpdater updater(reels, cfg, contentV2);
    user.recentInteractions.push_back(e);
    updater.apply(user, reels[e.reelId.value], e);
    return user;
}

} // namespace

// --- Gate-off (default ctor arg): modality estimates untouched, V1 behaviour intact -------------

// With contentV2 = false the three modality estimates stay EMPTY no matter what modality embeddings
// the reel carries — the pre-Phase-15 behaviour, byte-identical (D17).
TEST(ModalityUpdaterUnit, GateOffLeavesModalityEstimatesEmpty) {
    std::vector<Reel> reels{makeReel(0, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f})};
    LearningConfig cfg;
    const User out =
        applyOne(reels, makeUser({1.0f, 0.0f}), makeEvent(0, 1.0f), cfg, /*contentV2=*/false);

    EXPECT_TRUE(out.estimatedVisualPreference.empty());
    EXPECT_TRUE(out.estimatedMusicPreference.empty());
    EXPECT_TRUE(out.estimatedEmotionalPreference.empty());
}

// The gate is single-variable: with an IDENTICAL setup the V1 preference vectors are bit-identical
// whether contentV2 is off or on. The flag only adds the modality estimates; it never perturbs the
// V1 updates.
TEST(ModalityUpdaterUnit, ContentV2FlagDoesNotChangeV1Vectors) {
    std::vector<Reel> reels{makeReel(0, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f})};
    LearningConfig cfg;
    const InteractionEvent e = makeEvent(0, 0.6f);

    const User off = applyOne(reels, makeUser({1.0f, 0.0f}), e, cfg, /*contentV2=*/false);
    const User on = applyOne(reels, makeUser({1.0f, 0.0f}), e, cfg, /*contentV2=*/true);

    EXPECT_EQ(off.longTermPreference, on.longTermPreference);
    EXPECT_EQ(off.sessionPreference, on.sessionPreference);
    EXPECT_EQ(off.estimatedPreference, on.estimatedPreference);
    // ...and the flag DID populate the modality estimate (the added field).
    EXPECT_FALSE(on.estimatedVisualPreference.empty());
}

// The master learning switch dominates the gate: disabled learning is a strict no-op, so even with
// contentV2 = true no modality estimate is written.
TEST(ModalityUpdaterUnit, DisabledLearningSkipsModalityUpdate) {
    std::vector<Reel> reels{makeReel(0, {1.0f, 0.0f}, {0.0f, 1.0f})};
    LearningConfig cfg;
    cfg.enabled = false;
    const User out =
        applyOne(reels, makeUser({1.0f, 0.0f}), makeEvent(0, 1.0f), cfg, /*contentV2=*/true);
    EXPECT_TRUE(out.estimatedVisualPreference.empty());
}

// --- Cold start: an empty estimate seeds to the first observed modality direction ---------------

// The first observation of a modality with an EMPTY estimate seeds it to that reel's modality
// DIRECTION (unit-normalized), mirroring the 11.1 cold-start of the long-term vector to a prior.
// A deliberately non-unit {3,4} proves the seed is normalized to the direction {0.6,0.8}.
TEST(ModalityUpdaterUnit, EmptyEstimateColdStartsToReelModalityDirection) {
    std::vector<Reel> reels{makeReel(0, {1.0f, 0.0f}, {3.0f, 4.0f})};
    LearningConfig cfg;
    const User out =
        applyOne(reels, makeUser({1.0f, 0.0f}), makeEvent(0, 1.0f), cfg, /*contentV2=*/true);

    ASSERT_EQ(out.estimatedVisualPreference.size(), 2u);
    EXPECT_NEAR(out.estimatedVisualPreference[0], 0.6f, kTol);
    EXPECT_NEAR(out.estimatedVisualPreference[1], 0.8f, kTol);
    EXPECT_TRUE(isValid(out.estimatedVisualPreference));
}

// A reel that carries NO modality embedding (the gate-off default on the reel) is a no-op for that
// modality even under contentV2: an empty estimate stays empty.
TEST(ModalityUpdaterUnit, AbsentReelModalityEmbeddingLeavesEstimateEmpty) {
    // Visual present, music/emotional absent.
    std::vector<Reel> reels{makeReel(0, {1.0f, 0.0f}, {0.0f, 1.0f})};
    LearningConfig cfg;
    const User out =
        applyOne(reels, makeUser({1.0f, 0.0f}), makeEvent(0, 1.0f), cfg, /*contentV2=*/true);

    EXPECT_FALSE(out.estimatedVisualPreference.empty()); // visual was present -> learned
    EXPECT_TRUE(out.estimatedMusicPreference.empty());   // music absent -> untouched
    EXPECT_TRUE(out.estimatedEmotionalPreference.empty());
}

// --- EMA math (11.2 rule at modalityRate) on a non-empty estimate -------------------------------

// est = {1,0}, v = {0,1}, eta = 0.5, r = +1 => target = {0.5,0.5} => normalize {sqrt(1/2)}^2.
// Positive reward pulls the estimate toward the reel's modality direction (cosine rises).
TEST(ModalityUpdaterUnit, ModalityEmaPositiveRewardHandComputed) {
    std::vector<Reel> reels{makeReel(0, {1.0f, 0.0f}, {0.0f, 1.0f})};
    LearningConfig cfg;
    cfg.modalityRate = 0.5;

    User user = makeUser({1.0f, 0.0f});
    user.estimatedVisualPreference = {1.0f, 0.0f}; // pre-seeded (skip cold start)
    const Embedding v = {0.0f, 1.0f};
    const float cosBefore = dot(user.estimatedVisualPreference, v); // 0

    const User out = applyOne(reels, user, makeEvent(0, 1.0f), cfg, /*contentV2=*/true);

    EXPECT_NEAR(out.estimatedVisualPreference[0], 0.70710678f, kTol);
    EXPECT_NEAR(out.estimatedVisualPreference[1], 0.70710678f, kTol);
    EXPECT_GT(dot(out.estimatedVisualPreference, v), cosBefore);
    EXPECT_TRUE(isValid(out.estimatedVisualPreference));
}

// est = {1,0}, v = {0,1}, eta = 0.5, r = -1 => target = {0.5,-0.5} => normalize. Negative reward
// pushes the estimate AWAY from the reel's modality direction (cosine drops).
TEST(ModalityUpdaterUnit, ModalityEmaNegativeRewardPushesAway) {
    std::vector<Reel> reels{makeReel(0, {1.0f, 0.0f}, {0.0f, 1.0f})};
    LearningConfig cfg;
    cfg.modalityRate = 0.5;

    User user = makeUser({1.0f, 0.0f});
    user.estimatedVisualPreference = {1.0f, 0.0f};
    const Embedding v = {0.0f, 1.0f};

    const User out = applyOne(reels, user, makeEvent(0, -1.0f), cfg, /*contentV2=*/true);

    EXPECT_NEAR(out.estimatedVisualPreference[0], 0.70710678f, kTol);
    EXPECT_NEAR(out.estimatedVisualPreference[1], -0.70710678f, kTol);
    EXPECT_LT(dot(out.estimatedVisualPreference, v), 0.0f);
    EXPECT_TRUE(isValid(out.estimatedVisualPreference));
}

// The magnitude of the reward (the SAME observable reward that drives the V1 rule) scales the pull:
// a larger positive reward moves the estimate further toward the reel this step.
TEST(ModalityUpdaterUnit, LargerRewardMovesEstimateFurther) {
    std::vector<Reel> reels{makeReel(0, {1.0f, 0.0f}, {0.0f, 1.0f})};
    LearningConfig cfg;
    cfg.modalityRate = 0.2;
    const Embedding v = {0.0f, 1.0f};

    User base = makeUser({1.0f, 0.0f});
    base.estimatedVisualPreference = {1.0f, 0.0f};

    const User small = applyOne(reels, base, makeEvent(0, 0.25f), cfg, /*contentV2=*/true);
    const User large = applyOne(reels, base, makeEvent(0, 1.0f), cfg, /*contentV2=*/true);

    EXPECT_GT(dot(large.estimatedVisualPreference, v), dot(small.estimatedVisualPreference, v));
}

// Each of the three modalities updates from its OWN reel embedding independently.
TEST(ModalityUpdaterUnit, EachModalityUpdatesFromItsOwnEmbedding) {
    std::vector<Reel> reels{makeReel(0, {1.0f, 0.0f}, /*visual=*/{0.0f, 1.0f},
                                     /*music=*/{1.0f, 0.0f},
                                     /*emotional=*/{0.0f, -1.0f})};
    LearningConfig cfg;
    cfg.modalityRate = 0.5;

    User user = makeUser({1.0f, 0.0f});
    user.estimatedVisualPreference = {1.0f, 0.0f};
    user.estimatedMusicPreference = {1.0f, 0.0f};
    user.estimatedEmotionalPreference = {1.0f, 0.0f};

    const User out = applyOne(reels, user, makeEvent(0, 1.0f), cfg, /*contentV2=*/true);

    // visual: {1,0} toward {0,1} => {0.707, 0.707}
    EXPECT_NEAR(out.estimatedVisualPreference[0], 0.70710678f, kTol);
    EXPECT_NEAR(out.estimatedVisualPreference[1], 0.70710678f, kTol);
    // music: {1,0} toward {1,0} => stays {1,0}
    EXPECT_NEAR(out.estimatedMusicPreference[0], 1.0f, kTol);
    EXPECT_NEAR(out.estimatedMusicPreference[1], 0.0f, kTol);
    // emotional: {1,0} toward {0,-1} => {0.707, -0.707}
    EXPECT_NEAR(out.estimatedEmotionalPreference[0], 0.70710678f, kTol);
    EXPECT_NEAR(out.estimatedEmotionalPreference[1], -0.70710678f, kTol);
}
