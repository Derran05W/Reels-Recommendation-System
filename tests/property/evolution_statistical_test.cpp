// Phase 20 statistical directionality suite (contracts §7 A-row; V2 TDD §4.15/§4.16). Population-
// level, reduced-scale, deterministic (no rng in the mechanism — the population VARIATION comes
// from generateDataset's sampled plasticity/trust traits). Each margin is calibrated at the
// demonstrated operating point and documented AT the assert; the integrator may recalibrate ±
// (contracts §7).
//
// Headline (test b): SATISFACTION-DRIVEN, NOT REWARD-DRIVEN — the enabling half of exit criterion 1
// (Tier-4 acceptance 1). Two policies that agree on OBSERVED reward but disagree on HIDDEN
// satisfaction drive the population's hidden preferences in OPPOSITE directions. Package C
// demonstrates the full policy-level divergence at scale; this is the mechanism proof.

#include "rr/simulation/preference_evolution.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/simulation/dataset_generator.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"
#include "rr/simulation/hidden/latent_reaction.hpp"

using namespace rr;

namespace {

// A realistic hidden-user population (content_v2 on => sampled preferencePlasticity in [0,1] and
// platformTrust in [0.4,1], modality channels populated). Aggregated over several seeds for a
// stable sample.
std::vector<HiddenUserState> population(uint32_t usersPerSeed, uint32_t seeds) {
    SimulationConfig sc;
    sc.users = usersPerSeed;
    sc.reels = 40;
    sc.creators = 10;
    sc.topics = 8;
    sc.dimensions = 16;
    RealismConfig realism;
    realism.contentV2 = true;
    std::vector<HiddenUserState> all;
    for (uint32_t s = 1; s <= seeds; ++s) {
        GeneratedDataset ds = generateDataset(sc, realism, s);
        for (auto &h : ds.hiddenStates) {
            all.push_back(h);
        }
    }
    return all;
}

Reel probeReel(const Embedding &semantic) {
    Reel r{};
    r.id = ReelId{7};
    r.creatorId = CreatorId{3};
    r.embedding = semantic;
    r.primaryTopic = TopicId{4};
    r.durationSeconds = 30.0F;
    r.active = true;
    return r;
}

LatentReaction latentOf(float s, float regret = 0.0F) {
    LatentReaction l{};
    l.immediateSatisfaction = s;
    l.regret = regret;
    return l;
}

EvolutionConfig evoCfg(double eta) {
    EvolutionConfig c;
    c.etaEvo = eta;
    return c;
}

double mean(const std::vector<double> &v) {
    double acc = 0.0;
    for (double x : v) {
        acc += x;
    }
    return v.empty() ? 0.0 : acc / static_cast<double>(v.size());
}

} // namespace

// ============================================================================================
//  (b) SATISFACTION vs REWARD, at population scale. The SAME reel is served to two matched
//  populations. Both would look identically rewarding to an engagement policy (high watch/comment);
//  they differ only in HIDDEN satisfaction. applyImpression follows satisfaction, so the two
//  populations' hidden preferences diverge — one toward the reel, one away.
// ============================================================================================
TEST(EvolutionStatistical, SatisfactionNotRewardDrivesPopulationDivergence) {
    std::vector<HiddenUserState> users = population(/*usersPerSeed=*/60, /*seeds=*/4);
    ASSERT_GE(users.size(), 200u);

    // A fixed probe direction (unit vector on axis 0), served to every user 20 times.
    Embedding probeDir(16, 0.0F);
    probeDir[0] = 1.0F;
    const Reel reel = probeReel(probeDir);
    const uint32_t kImpressions = 20;
    PreferenceEvolution evo(evoCfg(0.1)); // operating point: eta_evo 0.1

    std::vector<double> ragebaitShift; // dot(final,probe) - dot(initial,probe), s<0
    std::vector<double> satisfyingShift;
    for (const HiddenUserState &base : users) {
        HiddenUserState rage = base;
        HiddenUserState good = base;
        const double d0 = dot(base.hiddenPreference, probeDir);
        for (uint32_t i = 0; i < kImpressions; ++i) {
            // Ragebait: POSITIVE observed reward (an engagement policy would chase it) but NEGATIVE
            // hidden satisfaction + high regret. Satisfying twin: same reel, POSITIVE satisfaction.
            evo.applyImpression(rage, reel, latentOf(/*s=*/-0.7F, /*regret=*/0.8F), 1000);
            evo.applyImpression(good, reel, latentOf(/*s=*/+0.7F), 1000);
        }
        ragebaitShift.push_back(dot(rage.hiddenPreference, probeDir) - d0);
        satisfyingShift.push_back(dot(good.hiddenPreference, probeDir) - d0);
    }

    const double rageMean = mean(ragebaitShift);
    const double goodMean = mean(satisfyingShift);
    // Opposite directions: satisfying reinforces TOWARD the reel, ragebait pushes AWAY. Margins at
    // the eta=0.1 / 20-impression operating point (empirically |shift| ~ 0.3-0.5 per arm).
    EXPECT_LT(rageMean, -0.05); // population moved AWAY from the ragebait reel
    EXPECT_GT(goodMean, 0.05);  // population moved TOWARD the satisfying reel
    // The policy-divergence signal: identical reel, opposite hidden satisfaction => the two
    // populations' mean alignment differs by a wide margin.
    EXPECT_GT(goodMean - rageMean, 0.2);
}

// ============================================================================================
//  (c)/(d) SATURATION + AVERSION directionality at scale: repeated regretful same-topic exposure
//  grows the mean exhaustion/aversion, so the mean exposure drag on future satisfaction deepens.
// ============================================================================================
TEST(EvolutionStatistical, RepeatedExposureDeepensMeanSatisfactionDrag) {
    std::vector<HiddenUserState> users = population(60, 3);
    Embedding dir(16, 0.0F);
    dir[0] = 1.0F;
    const Reel reel = probeReel(dir);
    PreferenceEvolution evo(evoCfg(0.05));

    // Mean exposure drag after 1, 5, 10 regretful same-topic exposures — should deepen
    // monotonically.
    std::vector<double> dragAt;
    const std::vector<uint32_t> checkpoints = {1, 5, 10};
    std::vector<HiddenUserState> work = users;
    uint32_t applied = 0;
    for (uint32_t target : checkpoints) {
        for (; applied < target; ++applied) {
            for (auto &h : work) {
                evo.applyImpression(h, reel, latentOf(/*s=*/-0.3F, /*regret=*/0.7F), 1000);
            }
        }
        std::vector<double> drags;
        for (const auto &h : work) {
            drags.push_back(exposureSatisfactionDelta(h, reel));
        }
        dragAt.push_back(mean(drags));
    }
    // Strictly deepening (more negative) mean drag with more exposure.
    EXPECT_LT(dragAt[1], dragAt[0]);
    EXPECT_LT(dragAt[2], dragAt[1]);
    EXPECT_LT(dragAt[2], -0.1); // by 10 exposures the mean drag is clearly negative
}

// ============================================================================================
//  (i) TRUST erosion under repeated regret, then recovery under satisfaction — at scale. A is the
//  only writer of retention.trust; it initializes from the platformTrust trait and clamps [0,1].
// ============================================================================================
TEST(EvolutionStatistical, TrustErodesUnderRegretThenRecovers) {
    std::vector<HiddenUserState> users = population(80, 3);
    Embedding dir(16, 0.0F);
    dir[0] = 1.0F;
    const Reel reel = probeReel(dir);
    PreferenceEvolution evo(evoCfg(0.02));

    std::vector<double> initTrust;
    for (const auto &h : users) {
        initTrust.push_back(std::clamp(static_cast<double>(h.platformTrust), 0.0, 1.0));
    }

    // 8 regretful/ragebait impressions erode trust.
    for (uint32_t i = 0; i < 8; ++i) {
        for (auto &h : users) {
            evo.applyImpression(h, reel, latentOf(/*s=*/-0.6F, /*regret=*/1.0F), 1000);
        }
    }
    std::vector<double> erodedTrust;
    for (const auto &h : users) {
        EXPECT_GE(h.retention.trust, 0.0); // clamp lower bound
        EXPECT_LE(h.retention.trust, 1.0); // clamp upper bound
        erodedTrust.push_back(h.retention.trust);
    }
    const double meanInit = mean(initTrust);
    const double meanEroded = mean(erodedTrust);
    EXPECT_LT(meanEroded, meanInit - 0.2); // trust clearly eroded from its platformTrust baseline

    // 20 satisfying impressions recover trust (slowly, but the mean rises).
    for (uint32_t i = 0; i < 20; ++i) {
        for (auto &h : users) {
            evo.applyImpression(h, reel, latentOf(/*s=*/1.0F), 1000);
        }
    }
    std::vector<double> recoveredTrust;
    for (const auto &h : users) {
        recoveredTrust.push_back(h.retention.trust);
    }
    EXPECT_GT(mean(recoveredTrust), meanEroded); // recovery direction is positive
}
