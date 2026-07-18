#include "rr/learning/online_user_state_updater.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/interaction.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"

// Seed-swept property tests for the Realism V2 gated per-modality EMA on rr::User (Phase 15, plan
// task 2). Mirrors Phase 7's convergence property (learning_property_test.cpp) for the modality
// channels. Properties asserted, each over >= 20 seeds:
//   * the estimated modality preference converges toward the direction of consistently positively-
//     rewarded reel modality embeddings (cosine to the true direction rises to ~1);
//   * every estimate stays unit-length after every apply;
//   * the V1 preference vectors are BIT-IDENTICAL with the contentV2 flag off vs on (the flag only
//     ADDS the modality estimates; it never changes the V1 updates);
//   * the modality estimates are deterministic (same inputs twice => bit-identical);
//   * the updater writes ONLY the User (the Reel catalog is untouched — belt-and-braces for the
//     structural guarantee that apply() takes Reel by const ref and no hidden state at all, D18).

using namespace rr;

namespace {

constexpr std::size_t kDim = 8;

Embedding randomUnit(Rng &rng) {
    Embedding e(kDim, 0.0f);
    for (std::size_t i = 0; i < kDim; ++i) {
        e[i] = static_cast<float>(rng.gaussian());
    }
    normalize(e);
    return e;
}

// A modality embedding clustered around `dir`: normalize(dir + noise * gaussian per component).
Embedding clusteredUnit(Rng &rng, const Embedding &dir, double noise) {
    Embedding e(kDim, 0.0f);
    for (std::size_t i = 0; i < kDim; ++i) {
        e[i] = static_cast<float>(static_cast<double>(dir[i]) + noise * rng.gaussian());
    }
    normalize(e);
    return e;
}

// The canonical basis vector along `axis`.
Embedding axisUnit(std::size_t axis) {
    Embedding e(kDim, 0.0f);
    e[axis] = 1.0f;
    return e;
}

InteractionEvent makeEvent(uint32_t reelId, float reward) {
    InteractionEvent e{};
    e.reelId = ReelId{reelId};
    e.type = InteractionType::CompleteWatch;
    e.reward = reward;
    e.sessionId = SessionId{1};
    return e;
}

// A cold-started user: V1 vectors set to `prior`, modality estimates left as caller sets them.
User makeUser(const Embedding &prior) {
    User u{};
    u.estimatedPreference = prior;
    u.longTermPreference = prior;
    u.sessionPreference = prior;
    return u;
}

bool sameEmbedding(const Embedding &a, const Embedding &b) { return a == b; }

// Deep equality of the reel fields the updater could conceivably read (V1 semantic + 3 modality
// embeddings) — the catalog must be identical before and after a learning pass.
bool reelUnchanged(const Reel &a, const Reel &b) {
    return sameEmbedding(a.embedding, b.embedding) &&
           sameEmbedding(a.visualStyleEmbedding, b.visualStyleEmbedding) &&
           sameEmbedding(a.musicEmbedding, b.musicEmbedding) &&
           sameEmbedding(a.emotionalToneEmbedding, b.emotionalToneEmbedding);
}

} // namespace

// The three estimated modality preferences converge toward the shared direction of consistently
// positively-rewarded reel modality embeddings; cosine to that direction rises from 0 (an
// orthogonal start) to ~1, and every estimate stays unit-length throughout.
TEST(ModalityLearningProperty, ConvergesTowardConsistentlyRewardedDirection) {
    constexpr double kNoise = 0.35;
    constexpr int kSteps = 120;

    for (int k = 0; k < 24; ++k) {
        const uint64_t seed = 3000ULL + static_cast<uint64_t>(k) * 7919ULL;
        Rng rng(seed);

        const Embedding target = axisUnit(0); // all reels cluster here
        std::vector<Reel> reels;
        reels.reserve(kSteps);
        for (int s = 0; s < kSteps; ++s) {
            Reel r{};
            r.id = ReelId{static_cast<uint32_t>(s)};
            r.embedding = randomUnit(rng); // semantic (drives the V1 session/long-term rules)
            r.visualStyleEmbedding = clusteredUnit(rng, target, kNoise);
            r.musicEmbedding = clusteredUnit(rng, target, kNoise);
            r.emotionalToneEmbedding = clusteredUnit(rng, target, kNoise);
            reels.push_back(std::move(r));
        }

        LearningConfig cfg;
        cfg.modalityRate = 0.2;
        OnlineUserStateUpdater updater(reels, cfg, /*contentV2=*/true);

        User user = makeUser(axisUnit(0));
        // Start every modality estimate ORTHOGONAL to the target (axis 1) so convergence is
        // visible.
        user.estimatedVisualPreference = axisUnit(1);
        user.estimatedMusicPreference = axisUnit(1);
        user.estimatedEmotionalPreference = axisUnit(1);

        const float cosBeforeVisual = dot(user.estimatedVisualPreference, target); // 0

        for (int s = 0; s < kSteps; ++s) {
            const InteractionEvent e = makeEvent(static_cast<uint32_t>(s), /*reward=*/1.0f);
            user.recentInteractions.push_back(e);
            if (user.recentInteractions.size() > cfg.recentWindow) {
                user.recentInteractions.pop_front();
            }
            updater.apply(user, reels[s], e);
            ASSERT_TRUE(isValid(user.estimatedVisualPreference)) << "seed " << seed;
            ASSERT_TRUE(isValid(user.estimatedMusicPreference)) << "seed " << seed;
            ASSERT_TRUE(isValid(user.estimatedEmotionalPreference)) << "seed " << seed;
        }

        const float cosVisual = dot(user.estimatedVisualPreference, target);
        const float cosMusic = dot(user.estimatedMusicPreference, target);
        const float cosEmotional = dot(user.estimatedEmotionalPreference, target);
        EXPECT_GT(cosVisual, cosBeforeVisual) << "seed " << seed;
        EXPECT_GT(cosVisual, 0.9f) << "seed " << seed << " cos " << cosVisual;
        EXPECT_GT(cosMusic, 0.9f) << "seed " << seed << " cos " << cosMusic;
        EXPECT_GT(cosEmotional, 0.9f) << "seed " << seed << " cos " << cosEmotional;
    }
}

// The contentV2 flag is single-variable: with an identical reward-driven interaction schedule the
// V1 preference vectors are BIT-IDENTICAL whether the flag is off or on. Off leaves the modality
// estimates empty; on populates them. The flag adds fields, it never changes V1 learning.
TEST(ModalityLearningProperty, V1VectorsByteIdenticalWithAndWithoutContentV2) {
    for (int k = 0; k < 24; ++k) {
        const uint64_t seed = 5000ULL + static_cast<uint64_t>(k) * 6151ULL;

        // Shared catalog + schedule (semantic and modality embeddings both present so both flag
        // settings do real V1 work and, for the ON run, real modality work).
        Rng build(seed);
        constexpr int kN = 60;
        std::vector<Reel> reels;
        reels.reserve(kN);
        for (int s = 0; s < kN; ++s) {
            Reel r{};
            r.id = ReelId{static_cast<uint32_t>(s)};
            r.embedding = randomUnit(build);
            r.visualStyleEmbedding = randomUnit(build);
            r.musicEmbedding = randomUnit(build);
            r.emotionalToneEmbedding = randomUnit(build);
            reels.push_back(std::move(r));
        }
        struct Step {
            uint32_t reelIdx;
            float reward;
        };
        std::vector<Step> schedule;
        schedule.reserve(kN);
        for (int s = 0; s < kN; ++s) {
            schedule.push_back({static_cast<uint32_t>(build.uniformInt(reels.size())),
                                static_cast<float>(build.uniform(-1.0, 1.0))});
        }

        LearningConfig cfg;
        auto run = [&](bool contentV2) {
            OnlineUserStateUpdater updater(reels, cfg, contentV2);
            User user = makeUser(axisUnit(0));
            for (const Step &st : schedule) {
                InteractionEvent e = makeEvent(st.reelIdx, st.reward);
                user.recentInteractions.push_back(e);
                if (user.recentInteractions.size() > cfg.recentWindow) {
                    user.recentInteractions.pop_front();
                }
                updater.apply(user, reels[st.reelIdx], e);
            }
            return user;
        };

        const User off = run(false);
        const User on = run(true);

        EXPECT_EQ(off.longTermPreference, on.longTermPreference) << "seed " << seed;
        EXPECT_EQ(off.sessionPreference, on.sessionPreference) << "seed " << seed;
        EXPECT_EQ(off.estimatedPreference, on.estimatedPreference) << "seed " << seed;
        // The flag's only visible effect: the modality estimates it adds.
        EXPECT_TRUE(off.estimatedVisualPreference.empty()) << "seed " << seed;
        EXPECT_FALSE(on.estimatedVisualPreference.empty()) << "seed " << seed;
        EXPECT_FALSE(on.estimatedMusicPreference.empty()) << "seed " << seed;
        EXPECT_FALSE(on.estimatedEmotionalPreference.empty()) << "seed " << seed;
    }
}

// Determinism (D8) + User-only writes: the same modality-learning pass run twice yields bit-
// identical estimates, and the Reel catalog is untouched (the updater mutates only the User).
TEST(ModalityLearningProperty, DeterministicAndCatalogUntouched) {
    for (int k = 0; k < 22; ++k) {
        const uint64_t seed = 9000ULL + static_cast<uint64_t>(k) * 5381ULL;
        Rng build(seed);
        constexpr int kN = 50;
        std::vector<Reel> reels;
        reels.reserve(kN);
        for (int s = 0; s < kN; ++s) {
            Reel r{};
            r.id = ReelId{static_cast<uint32_t>(s)};
            r.embedding = randomUnit(build);
            r.visualStyleEmbedding = randomUnit(build);
            r.musicEmbedding = randomUnit(build);
            r.emotionalToneEmbedding = randomUnit(build);
            reels.push_back(std::move(r));
        }
        const std::vector<Reel> reelsSnapshot = reels; // deep copy for the untouched-catalog check

        LearningConfig cfg;
        cfg.modalityRate = 0.1;
        auto run = [&]() {
            OnlineUserStateUpdater updater(reels, cfg, /*contentV2=*/true);
            User user = makeUser(axisUnit(0));
            user.estimatedVisualPreference = axisUnit(2);
            Rng pick(seed ^ 0xabcdef01ULL);
            for (int s = 0; s < kN; ++s) {
                const auto idx = static_cast<uint32_t>(pick.uniformInt(reels.size()));
                InteractionEvent e = makeEvent(idx, static_cast<float>(pick.uniform(-1.0, 1.0)));
                user.recentInteractions.push_back(e);
                if (user.recentInteractions.size() > cfg.recentWindow) {
                    user.recentInteractions.pop_front();
                }
                updater.apply(user, reels[idx], e);
            }
            return user;
        };

        const User a = run();
        const User b = run();
        EXPECT_EQ(a.estimatedVisualPreference, b.estimatedVisualPreference) << "seed " << seed;
        EXPECT_EQ(a.estimatedMusicPreference, b.estimatedMusicPreference) << "seed " << seed;
        EXPECT_EQ(a.estimatedEmotionalPreference, b.estimatedEmotionalPreference)
            << "seed " << seed;

        ASSERT_EQ(reels.size(), reelsSnapshot.size());
        for (std::size_t i = 0; i < reels.size(); ++i) {
            EXPECT_TRUE(reelUnchanged(reels[i], reelsSnapshot[i]))
                << "seed " << seed << " reel " << i;
        }
    }
}
