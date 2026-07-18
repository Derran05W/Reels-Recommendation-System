#include "rr/simulation/latent_model.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/creator.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/cohort_hash.hpp"
#include "rr/simulation/dataset_generator.hpp"
#include "rr/simulation/hidden/hidden_reel_state.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"

using namespace rr;

namespace {

// A random L2-normalized embedding.
Embedding randomUnit(Rng &rng, int dim) {
    Embedding e(dim, 0.0f);
    for (float &c : e) {
        c = static_cast<float>(rng.gaussian());
    }
    normalize(e);
    return e;
}

struct Impression {
    HiddenUserState user;
    Reel reel;
    HiddenReelState hiddenReel;
    Creator creator;
};

// A fully-populated random impression exercising every branch (channels, scalars, both languages,
// and a random archetype's hidden biases / niche cohort).
Impression buildImpression(Rng &rng, uint32_t userId) {
    constexpr int kDim = 8;
    Impression im;
    im.user.userId = UserId{userId};
    im.user.hiddenPreference = randomUnit(rng, kDim);
    im.user.visualPreference = randomUnit(rng, kDim);
    im.user.musicPreference = randomUnit(rng, kDim);
    im.user.emotionalPreference = randomUnit(rng, kDim);
    im.user.usefulnessPreference = static_cast<float>(rng.uniform01());
    im.user.humourPreference = static_cast<float>(rng.uniform01());
    im.user.noveltySeeking = static_cast<float>(rng.uniform01());
    im.user.controversyTolerance = static_cast<float>(rng.uniform01());
    im.user.clickbaitSusceptibility = static_cast<float>(rng.uniform01());
    im.user.informationTolerance = static_cast<float>(rng.uniform01());
    im.user.languageMismatchTolerance = static_cast<float>(rng.uniform01());
    im.user.primaryLanguage = LanguageId{static_cast<uint32_t>(rng.uniformInt(4))};

    im.reel.id = ReelId{userId};
    im.reel.embedding = randomUnit(rng, kDim);
    im.reel.visualStyleEmbedding = randomUnit(rng, kDim);
    im.reel.musicEmbedding = randomUnit(rng, kDim);
    im.reel.emotionalToneEmbedding = randomUnit(rng, kDim);
    im.reel.usefulness = static_cast<float>(rng.uniform01());
    im.reel.humour = static_cast<float>(rng.uniform01());
    im.reel.novelty = static_cast<float>(rng.uniform01());
    im.reel.controversy = static_cast<float>(rng.uniform01());
    im.reel.informationDensity = static_cast<float>(rng.uniform01());
    im.reel.emotionalIntensity = static_cast<float>(rng.uniform01());
    im.reel.language = LanguageId{static_cast<uint32_t>(rng.uniformInt(4))};

    const auto &catalog = RealismConfig{}.archetypes;
    const auto &spec = catalog[rng.uniformInt(catalog.size())];
    im.hiddenReel.satisfactionBias = static_cast<float>(spec.satisfactionBias);
    im.hiddenReel.regretBias = static_cast<float>(spec.regretBias);
    im.hiddenReel.comfortReturnBonus = static_cast<float>(spec.comfortReturnBonus);
    im.hiddenReel.nicheCohortWidth = static_cast<float>(spec.nicheCohortWidth);
    im.hiddenReel.nicheCohortCentre = static_cast<float>(rng.uniform01());

    im.creator.id = CreatorId{0};
    im.creator.styleEmbedding = randomUnit(rng, kDim);
    return im;
}

bool sameReaction(const LatentReaction &a, const LatentReaction &b) {
    return a.immediateSatisfaction == b.immediateSatisfaction &&
           a.informationalValue == b.informationalValue && a.emotionalValue == b.emotionalValue &&
           a.regret == b.regret && a.desireForSimilarContent == b.desireForSimilarContent &&
           a.fatigueDelta == b.fatigueDelta;
}

} // namespace

// --- Determinism: same inputs + same seed => identical reaction (>= 20 seeds) ------------------

TEST(LatentModelProperty, DeterministicAcrossSeeds) {
    BehaviourV2Config cfg;
    for (uint64_t seed = 1; seed <= 25; ++seed) {
        Rng build(seed);
        Impression im = buildImpression(build, static_cast<uint32_t>(seed));

        Rng a(seed * 7919 + 1);
        Rng b(seed * 7919 + 1);
        LatentReaction ra =
            computeLatentReaction(cfg, im.user, im.reel, im.hiddenReel, im.creator, a);
        LatentReaction rb =
            computeLatentReaction(cfg, im.user, im.reel, im.hiddenReel, im.creator, b);
        EXPECT_TRUE(sameReaction(ra, rb)) << "seed " << seed;
    }
}

// --- Fixed draw count: exactly ONE gaussian, independent of archetype/branch (>= 20 seeds) -----

TEST(LatentModelProperty, FixedDrawCountIsExactlyOneGaussianRegardlessOfBranch) {
    BehaviourV2Config cfg;
    for (uint64_t seed = 1; seed <= 25; ++seed) {
        // Two structurally very different impressions (different archetype, niche membership,
        // language match, controversy, ...) must advance the "satisfaction" stream identically.
        Rng buildA(seed);
        Rng buildB(seed + 100000);
        Impression imA = buildImpression(buildA, static_cast<uint32_t>(seed));
        Impression imB = buildImpression(buildB, static_cast<uint32_t>(seed) + 1);

        Rng a(seed);
        Rng b(seed);
        computeLatentReaction(cfg, imA.user, imA.reel, imA.hiddenReel, imA.creator, a);
        computeLatentReaction(cfg, imB.user, imB.reel, imB.hiddenReel, imB.creator, b);
        const uint64_t canaryA = a.nextU64();
        const uint64_t canaryB = b.nextU64();
        EXPECT_EQ(canaryA, canaryB) << "seed " << seed << ": stream advanced by different amounts";

        // And that amount equals exactly one gaussian() draw.
        Rng ref(seed);
        (void)ref.gaussian();
        EXPECT_EQ(ref.nextU64(), canaryA) << "seed " << seed << ": not exactly one gaussian";
    }
}

// --- latentNoiseStd == 0 still draws (fixed count preserved) ------------------------------------

TEST(LatentModelProperty, ZeroNoiseStillConsumesOneDraw) {
    BehaviourV2Config cfg;
    cfg.latentNoiseStd = 0.0;
    Rng build(3);
    Impression im = buildImpression(build, 3);
    Rng a(5);
    computeLatentReaction(cfg, im.user, im.reel, im.hiddenReel, im.creator, a);
    Rng ref(5);
    (void)ref.gaussian();
    EXPECT_EQ(a.nextU64(), ref.nextU64());
}

// --- No input mutation -------------------------------------------------------------------------

TEST(LatentModelProperty, DoesNotMutateInputs) {
    BehaviourV2Config cfg;
    Rng build(11);
    Impression im = buildImpression(build, 11);
    const HiddenUserState userBefore = im.user;
    const Reel reelBefore = im.reel;
    const HiddenReelState hrBefore = im.hiddenReel;
    const Creator creatorBefore = im.creator;
    const BehaviourV2Config cfgBefore = cfg;

    Rng rng(13);
    computeLatentReaction(cfg, im.user, im.reel, im.hiddenReel, im.creator, rng);

    EXPECT_EQ(im.user.hiddenPreference, userBefore.hiddenPreference);
    EXPECT_EQ(im.user.musicPreference, userBefore.musicPreference);
    EXPECT_EQ(im.user.usefulnessPreference, userBefore.usefulnessPreference);
    EXPECT_EQ(im.user.clickbaitSusceptibility, userBefore.clickbaitSusceptibility);
    EXPECT_EQ(im.reel.embedding, reelBefore.embedding);
    EXPECT_EQ(im.reel.usefulness, reelBefore.usefulness);
    EXPECT_EQ(im.reel.controversy, reelBefore.controversy);
    EXPECT_EQ(im.hiddenReel.satisfactionBias, hrBefore.satisfactionBias);
    EXPECT_EQ(im.hiddenReel.nicheCohortCentre, hrBefore.nicheCohortCentre);
    EXPECT_EQ(im.creator.styleEmbedding, creatorBefore.styleEmbedding);
    EXPECT_TRUE(cfg == cfgBefore);
}

// ================================================================================================
// Latent-side statistical calibration (fixed seed). Package C's suite asserts the COMBINED
// system; these assert the LATENT side in isolation so the combined targets are achievable.
// ================================================================================================

namespace {

constexpr uint64_t kStatSeed = 20260718;

struct Accum {
    double satisfaction = 0.0;
    double regret = 0.0;
    double desire = 0.0;
    double informational = 0.0;
    double emotional = 0.0;
    long n = 0;
    void add(const LatentReaction &r) {
        satisfaction += r.immediateSatisfaction;
        regret += r.regret;
        desire += r.desireForSimilarContent;
        informational += r.informationalValue;
        emotional += r.emotionalValue;
        ++n;
    }
    double meanSat() const { return satisfaction / n; }
    double meanRegret() const { return regret / n; }
    double meanDesire() const { return desire / n; }
    double meanInfo() const { return informational / n; }
    double meanEmo() const { return emotional / n; }
};

int archetypeIndexByName(const std::vector<ArchetypeSpec> &catalog, const std::string &name) {
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        if (catalog[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

TEST(LatentModelStatistical, CohortSignaturesMatchCalibrationTargets) {
    SimulationConfig sim;
    sim.seed = kStatSeed;
    sim.reels = 12000;
    sim.users = 1200;
    sim.creators = 600;
    sim.topics = 32;
    sim.dimensions = 48;

    RealismConfig realism;
    realism.contentV2 = true;
    const auto &catalog = realism.archetypes;
    const std::size_t nArch = catalog.size();

    GeneratedDataset ds = generateDataset(sim, realism, kStatSeed);
    ASSERT_EQ(ds.reels.size(), ds.hiddenReelStates.size());
    ASSERT_FALSE(ds.hiddenStates.empty());

    // Bucket reel indices by archetype.
    std::vector<std::vector<uint32_t>> byArch(nArch);
    for (std::size_t i = 0; i < ds.hiddenReelStates.size(); ++i) {
        byArch[ds.hiddenReelStates[i].archetypeIndex].push_back(static_cast<uint32_t>(i));
    }

    BehaviourV2Config cfg;
    Rng pick = forkRng(kStatSeed, "pick");
    Rng satisfaction = forkRng(kStatSeed, "satisfaction");

    std::vector<Accum> perArch(nArch);
    Accum population;
    Accum nicheInside, nicheOutside;
    const int nicheIdx = archetypeIndexByName(catalog, "niche_treasure");
    constexpr int kPairsPerArch = 4000;

    for (std::size_t arch = 0; arch < nArch; ++arch) {
        const auto &reels = byArch[arch];
        if (reels.empty()) {
            continue;
        }
        for (int p = 0; p < kPairsPerArch; ++p) {
            const uint32_t reelIdx = reels[pick.uniformInt(reels.size())];
            const auto &reel = ds.reels[reelIdx];
            const auto &hidden = ds.hiddenStates[pick.uniformInt(ds.hiddenStates.size())];
            const auto &creator = ds.creators[reel.creatorId.value];
            const auto &hiddenReel = ds.hiddenReelStates[reelIdx];

            LatentReaction lr =
                computeLatentReaction(cfg, hidden, reel, hiddenReel, creator, satisfaction);
            perArch[arch].add(lr);
            population.add(lr);

            if (static_cast<int>(arch) == nicheIdx && hiddenReel.nicheCohortWidth > 0.0f) {
                const double h = cohortHash01(hidden.userId);
                const double dist = std::abs(h - static_cast<double>(hiddenReel.nicheCohortCentre));
                if (dist <= static_cast<double>(hiddenReel.nicheCohortWidth)) {
                    nicheInside.add(lr);
                } else {
                    nicheOutside.add(lr);
                }
            }
        }
    }

    const int iGen = archetypeIndexByName(catalog, "genuinely_satisfying");
    const int iUseful = archetypeIndexByName(catalog, "useful");
    const int iRage = archetypeIndexByName(catalog, "ragebait");
    const int iClick = archetypeIndexByName(catalog, "clickbait");
    const int iComfort = archetypeIndexByName(catalog, "comfort");
    const int iPolished = archetypeIndexByName(catalog, "polished_irrelevant");
    const int iMusic = archetypeIndexByName(catalog, "background_music");

    // Report the measured cohort table (summary lines only).
    std::cout << "\n[latent-stat] seed=" << kStatSeed << " pairs/arch=" << kPairsPerArch << "\n";
    std::cout << "[latent-stat] archetype              sat     regret  desire  info    emo\n";
    for (std::size_t a = 0; a < nArch; ++a) {
        std::printf("[latent-stat] %-20s % .3f  %.3f   % .3f  %.3f   %.3f\n",
                    catalog[a].name.c_str(), perArch[a].meanSat(), perArch[a].meanRegret(),
                    perArch[a].meanDesire(), perArch[a].meanInfo(), perArch[a].meanEmo());
    }
    std::printf("[latent-stat] %-20s % .3f  %.3f   % .3f  %.3f   %.3f\n", "POPULATION",
                population.meanSat(), population.meanRegret(), population.meanDesire(),
                population.meanInfo(), population.meanEmo());
    std::printf("[latent-stat] niche inside sat=%.3f (n=%ld)  outside sat=%.3f (n=%ld)\n",
                nicheInside.meanSat(), nicheInside.n, nicheOutside.meanSat(), nicheOutside.n);

    const double popSat = population.meanSat();
    const double popRegret = population.meanRegret();
    const double popDesire = population.meanDesire();

    // (1) Ragebait: negative mean satisfaction, well below population; regret elevated.
    EXPECT_LT(perArch[iRage].meanSat(), 0.0);
    EXPECT_LT(perArch[iRage].meanSat(), popSat - 0.3);
    EXPECT_GT(perArch[iRage].meanRegret(), popRegret + 0.15);

    // (2) Useful: satisfaction among the top archetype means, and highest informationalValue.
    {
        std::vector<double> sats(nArch);
        for (std::size_t a = 0; a < nArch; ++a) {
            sats[a] = perArch[a].meanSat();
        }
        std::sort(sats.begin(), sats.end(), std::greater<double>());
        const double thirdHighest = sats[2];
        EXPECT_GE(perArch[iUseful].meanSat(), thirdHighest); // in the top 3
        EXPECT_GT(perArch[iUseful].meanSat(), popSat + 0.1);
        EXPECT_GT(perArch[iUseful].meanInfo(), 0.55);
        for (std::size_t a = 0; a < nArch; ++a) {
            if (static_cast<int>(a) != iUseful) {
                EXPECT_GE(perArch[iUseful].meanInfo(), perArch[a].meanInfo())
                    << "useful informationalValue not the highest vs " << catalog[a].name;
            }
        }
    }

    // (3) Clickbait: regret markedly above population.
    EXPECT_GT(perArch[iClick].meanRegret(), popRegret + 0.1);

    // (4) Background-music: satisfaction stays POSITIVE (music/emotional driven) at weak topic
    //     match; emotionalValue clearly positive.
    EXPECT_GT(perArch[iMusic].meanSat(), 0.05);
    EXPECT_GT(perArch[iMusic].meanEmo(), 0.15);

    // (5) Comfort: desireForSimilarContent above population.
    EXPECT_GT(perArch[iComfort].meanDesire(), popDesire + 0.05);

    // (6) Niche treasure: inside-cohort satisfaction MUCH higher than outside.
    ASSERT_GT(nicheInside.n, 100);
    ASSERT_GT(nicheOutside.n, 100);
    EXPECT_GT(nicheInside.meanSat(), nicheOutside.meanSat() + 0.3);

    // Sanity: the genuinely-satisfying and polished-irrelevant poles behave as designed.
    EXPECT_GT(perArch[iGen].meanSat(), popSat);
    EXPECT_LT(perArch[iPolished].meanDesire(), popDesire); // low lasting value
}
