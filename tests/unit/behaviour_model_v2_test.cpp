// BehaviourModelV2 unit tests (Phase 14, V2 TDD 4.3/4.4). Package A's real multi-channel latent
// is a neutral stub in package B's tree, so the latent-conditioned OBSERVABLE sampling is tested
// by driving BehaviourModelV2::sampleObservables with SYNTHETIC LatentReaction values constructed
// directly across a grid — exactly the mechanism the phase brief prescribes. Each signature wedge
// (completed-because-short, hook/decay clickbait shape, social-conformity likes, useful-underliked
// damping, ragebait comments, emotion-driven rewatch, not-interested, watch<->satisfaction
// monotone-but-noisy) gets a named test with constructed inputs. Determinism and the byte-identity
// of the untouched V1 Simulator::step path are asserted too.

#include "rr/simulation/behaviour_model_v2.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/creator.hpp"
#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/behaviour_model.hpp"
#include "rr/simulation/hidden/hidden_reel_state.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"
#include "rr/simulation/simulator.hpp"

using namespace rr;

namespace {

Embedding vec4(float a, float b, float c, float d) { return Embedding{a, b, c, d}; }

// Neutral hidden user: preference on axis 0, moderate V1 propensities. Channel embeddings are set
// but never consulted here (they feed package A's latent, which we bypass). Callers override the
// single field a test isolates.
HiddenUserState makeUser() {
    HiddenUserState h{};
    h.userId = UserId{1};
    h.hiddenPreference = vec4(1, 0, 0, 0);
    h.visualPreference = vec4(0, 1, 0, 0);
    h.musicPreference = vec4(0, 0, 1, 0);
    h.emotionalPreference = vec4(0, 0, 0, 1);
    h.durationTolerance = 0.5F;
    h.avgSessionLength = 20.0F;
    h.likePropensity = 0.12F;
    h.sharePropensity = 0.04F;
    h.clickbaitSusceptibility = 0.5F;
    return h;
}

// Neutral reel: not short (60s), bland attributes, no visible popularity. Callers override.
Reel makeReel() {
    Reel r{};
    r.id = ReelId{10};
    r.creatorId = CreatorId{2};
    r.embedding = vec4(0, 0, 1, 0);
    r.visualStyleEmbedding = vec4(0, 1, 0, 0);
    r.musicEmbedding = vec4(0, 0, 1, 0);
    r.emotionalToneEmbedding = vec4(0, 0, 0, 1);
    r.intrinsicQuality = 0.5F;
    r.durationSeconds = 60.0F;
    r.primaryTopic = TopicId{0};
    r.impressionCount = 0;
    r.likeCount = 0;
    r.active = true;
    return r;
}

// Creator whose style is orthogonal to the user preference -> zero true creator affinity (neutral
// for follow / profile-visit), unless a test aligns it.
Creator makeCreator(Embedding style = vec4(0, 1, 0, 0)) {
    Creator c{};
    c.id = CreatorId{2};
    c.styleEmbedding = std::move(style);
    c.baseQuality = 0.5F;
    return c;
}

HiddenReelState makeHiddenReel(float openingHook = 0.0F, float retentionDecay = 0.0F) {
    HiddenReelState hr{};
    hr.reelId = ReelId{10};
    hr.openingHook = openingHook;
    hr.retentionDecay = retentionDecay;
    return hr;
}

LatentReaction makeLatent(float satisfaction, float info = 0.0F, float emo = 0.0F,
                          float regret = 0.0F, float desire = 0.0F) {
    LatentReaction l{};
    l.immediateSatisfaction = satisfaction;
    l.informationalValue = info;
    l.emotionalValue = emo;
    l.regret = regret;
    l.desireForSimilarContent = desire;
    return l;
}

BehaviourModelV2 makeModel() { return BehaviourModelV2(BehaviourConfig{}, BehaviourV2Config{}); }

// Average-rank Spearman rank correlation (handles the heavy ties the watch-ratio bands produce).
double spearman(const std::vector<double> &x, const std::vector<double> &y) {
    const auto ranks = [](const std::vector<double> &v) {
        const size_t n = v.size();
        std::vector<size_t> idx(n);
        std::iota(idx.begin(), idx.end(), size_t{0});
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return v[a] < v[b]; });
        std::vector<double> r(n);
        size_t i = 0;
        while (i < n) {
            size_t j = i;
            while (j + 1 < n && v[idx[j + 1]] == v[idx[i]]) {
                ++j;
            }
            const double avgRank = static_cast<double>(i + j) / 2.0 + 1.0;
            for (size_t k = i; k <= j; ++k) {
                r[idx[k]] = avgRank;
            }
            i = j + 1;
        }
        return r;
    };
    const std::vector<double> rx = ranks(x);
    const std::vector<double> ry = ranks(y);
    const size_t n = rx.size();
    const double mx = std::accumulate(rx.begin(), rx.end(), 0.0) / static_cast<double>(n);
    const double my = std::accumulate(ry.begin(), ry.end(), 0.0) / static_cast<double>(n);
    double num = 0, dx = 0, dy = 0;
    for (size_t i = 0; i < n; ++i) {
        const double a = rx[i] - mx;
        const double b = ry[i] - my;
        num += a * b;
        dx += a * a;
        dy += b * b;
    }
    return num / std::sqrt(dx * dy);
}

// Rate at which `pred(outcome)` fires over `n` sampleObservables draws on one behaviour stream.
template <typename Pred>
double rate(const BehaviourModelV2 &model, const HiddenUserState &h, const Reel &r,
            const HiddenReelState &hr, const Creator &c, const LatentReaction &latent,
            uint64_t seed, int n, Pred pred) {
    Rng rng(seed);
    int hits = 0;
    for (int i = 0; i < n; ++i) {
        if (pred(model.sampleObservables(h, r, hr, c, latent, rng))) {
            ++hits;
        }
    }
    return static_cast<double>(hits) / n;
}

} // namespace

// --- Determinism: identical rng state + identical inputs => identical outcome (and latent) -------
TEST(BehaviourModelV2Test, BitIdenticalOutcomeForSameRngState) {
    const BehaviourModelV2 model = makeModel();
    const HiddenUserState h = makeUser();
    const Reel r = makeReel();
    const HiddenReelState hr = makeHiddenReel(0.4F, 0.2F);
    const Creator c = makeCreator(vec4(0.6F, 0.4F, 0, 0));
    const LatentReaction latent = makeLatent(0.5F, 0.6F, 0.7F, 0.1F, 0.3F);

    // sampleObservables: two fresh streams at the same seed produce identical sequences.
    Rng a(4242), b(4242);
    for (int i = 0; i < 200; ++i) {
        const BehaviourOutcome oa = model.sampleObservables(h, r, hr, c, latent, a);
        const BehaviourOutcome ob = model.sampleObservables(h, r, hr, c, latent, b);
        EXPECT_EQ(oa.completed, ob.completed);
        EXPECT_EQ(oa.instantSkip, ob.instantSkip);
        EXPECT_EQ(oa.watchRatio, ob.watchRatio);
        EXPECT_EQ(oa.replayCount, ob.replayCount);
        EXPECT_EQ(oa.commented, ob.commented);
        EXPECT_EQ(oa.saved, ob.saved);
        EXPECT_EQ(oa.profileVisited, ob.profileVisited);
        EXPECT_EQ(oa.liked, ob.liked);
        EXPECT_EQ(oa.primaryType, ob.primaryType);
    }

    // simulate(): identical outcome AND identical latent for identical rng states (with the stubbed
    // neutral latent this is the whole pipeline's determinism contract).
    Rng behA(7), behB(7), satA(9), satB(9);
    LatentReaction la, lb;
    const BehaviourOutcome oa = model.simulate(h, r, hr, c, behA, satA, la);
    const BehaviourOutcome ob = model.simulate(h, r, hr, c, behB, satB, lb);
    EXPECT_EQ(oa.watchRatio, ob.watchRatio);
    EXPECT_EQ(oa.commented, ob.commented);
    EXPECT_EQ(la.immediateSatisfaction, lb.immediateSatisfaction);
    EXPECT_EQ(la.regret, lb.regret);
}

// --- Completed-because-short (V2 TDD 3.2): completion inflated for short reels, latent-independent
TEST(BehaviourModelV2Test, ShortDurationInflatesCompletionIndependentOfLatent) {
    const BehaviourModelV2 model = makeModel();
    const HiddenUserState h = makeUser();
    const HiddenReelState hr = makeHiddenReel();
    const Creator c = makeCreator();
    const LatentReaction neutral = makeLatent(0.0F); // no latent signal either way

    Reel shortReel = makeReel();
    shortReel.durationSeconds = 6.0F; // < BehaviourV2Config::shortDurationSeconds (12s default)
    Reel longReel = makeReel();
    longReel.durationSeconds = 60.0F;

    const auto isComplete = [](const BehaviourOutcome &o) { return o.completed; };
    const double shortRate = rate(model, h, shortReel, hr, c, neutral, 1234, 4000, isComplete);
    const double longRate = rate(model, h, longReel, hr, c, neutral, 1234, 4000, isComplete);

    EXPECT_GT(shortRate, longRate + 0.25)
        << "short=" << shortRate << " long=" << longRate
        << " — short reels must complete more often even with identical (neutral) latent";
}

// --- Clickbait signature part 1: opening hook SUPPRESSES instant-skip ----------------------------
TEST(BehaviourModelV2Test, OpeningHookSuppressesInstantSkip) {
    const BehaviourModelV2 model = makeModel();
    const HiddenUserState h = makeUser();
    const Reel r = makeReel();
    const Creator c = makeCreator();
    const LatentReaction neutral = makeLatent(0.0F);

    const auto isSkip = [](const BehaviourOutcome &o) { return o.instantSkip; };
    const double noHook =
        rate(model, h, r, makeHiddenReel(0.0F, 0.0F), c, neutral, 55, 5000, isSkip);
    const double hooked =
        rate(model, h, r, makeHiddenReel(0.9F, 0.0F), c, neutral, 55, 5000, isSkip);

    EXPECT_LT(hooked, noHook - 0.2)
        << "hooked=" << hooked << " noHook=" << noHook
        << " — a strong opening hook must pull instant-skip clearly below population";
}

// --- Clickbait signature part 2: retention decay SUPPRESSES completion ---------------------------
TEST(BehaviourModelV2Test, RetentionDecaySuppressesCompletion) {
    const BehaviourModelV2 model = makeModel();
    const HiddenUserState h = makeUser();
    const Reel r = makeReel(); // 60s, not short -> no completion boost masking the decay
    const Creator c = makeCreator();
    const LatentReaction neutral = makeLatent(0.0F);

    const auto isComplete = [](const BehaviourOutcome &o) { return o.completed; };
    const double noDecay =
        rate(model, h, r, makeHiddenReel(0.0F, 0.0F), c, neutral, 88, 5000, isComplete);
    const double decayed =
        rate(model, h, r, makeHiddenReel(0.0F, 0.9F), c, neutral, 88, 5000, isComplete);

    EXPECT_LT(decayed, noDecay - 0.2)
        << "decayed=" << decayed << " noDecay=" << noDecay
        << " — strong retention decay must pull completion clearly below population (clickbait "
           "early-abandonment signature)";
}

// --- Social conformity: visible popularity counters lift the like rate ---------------------------
TEST(BehaviourModelV2Test, SocialConformityLiftsLikeFromVisiblePopularity) {
    const BehaviourModelV2 model = makeModel();
    const HiddenUserState h = makeUser();
    const HiddenReelState hr = makeHiddenReel();
    const Creator c = makeCreator();
    const LatentReaction latent = makeLatent(0.6F); // positive -> completion likely (like is gated)

    Reel popular = makeReel();
    popular.impressionCount = 100;
    popular.likeCount = 90; // visible like rate 0.9
    Reel unpopular = makeReel();
    unpopular.impressionCount = 100;
    unpopular.likeCount = 0; // visible like rate 0.0

    const auto isLike = [](const BehaviourOutcome &o) { return o.liked; };
    const double popRate = rate(model, h, popular, hr, c, latent, 321, 8000, isLike);
    const double unpopRate = rate(model, h, unpopular, hr, c, latent, 321, 8000, isLike);

    EXPECT_GT(popRate, unpopRate + 0.03)
        << "popular=" << popRate << " unpopular=" << unpopRate
        << " — identical latent, only visible popularity differs: the popular reel must be liked "
           "more (social conformity)";
}

// --- Useful content is under-liked but heavily saved (like damping when info dominates emo)
// -------
TEST(BehaviourModelV2Test, UsefulContentUnderLikedButSavedMoreThanEmotional) {
    const BehaviourModelV2 model = makeModel();
    const HiddenUserState h = makeUser();
    const HiddenReelState hr = makeHiddenReel();
    const Creator c = makeCreator();
    const Reel r = makeReel();

    // Same top-tier satisfaction; the ONLY difference is info-vs-emo balance.
    const LatentReaction useful = makeLatent(0.7F, /*info=*/0.9F, /*emo=*/0.1F);
    const LatentReaction emotional = makeLatent(0.7F, /*info=*/0.1F, /*emo=*/0.9F);

    const auto isLike = [](const BehaviourOutcome &o) { return o.liked; };
    const auto isSave = [](const BehaviourOutcome &o) { return o.saved; };

    const double usefulLike = rate(model, h, r, hr, c, useful, 7, 8000, isLike);
    const double emoLike = rate(model, h, r, hr, c, emotional, 7, 8000, isLike);
    const double usefulSave = rate(model, h, r, hr, c, useful, 7, 8000, isSave);
    const double emoSave = rate(model, h, r, hr, c, emotional, 7, 8000, isSave);

    EXPECT_LT(usefulLike, emoLike)
        << "usefulLike=" << usefulLike << " emoLike=" << emoLike
        << " — useful content (info-dominant) must be liked LESS than equally-satisfying emotional "
           "content";
    EXPECT_GT(usefulSave, emoSave + 0.1)
        << "usefulSave=" << usefulSave << " emoSave=" << emoSave
        << " — ... while its saves run clearly higher (useful's co-signal)";
}

// --- Ragebait: comment rate above population even at NEGATIVE satisfaction
// ------------------------
TEST(BehaviourModelV2Test, RagebaitCommentsAboveBaselineEvenWhenUnsatisfying) {
    const BehaviourModelV2 model = makeModel();
    HiddenUserState h = makeUser();
    h.clickbaitSusceptibility = 0.6F;
    const HiddenReelState hr = makeHiddenReel();
    const Creator c = makeCreator();
    const LatentReaction ragebaitLatent = makeLatent(-0.5F, /*info=*/0.0F, /*emo=*/0.8F);

    Reel ragebait = makeReel();
    ragebait.controversy = 0.9F;
    ragebait.emotionalIntensity = 0.9F;
    ragebait.clickbaitStrength = 0.5F;
    Reel bland = makeReel();
    bland.controversy = 0.05F;
    bland.emotionalIntensity = 0.1F;
    bland.clickbaitStrength = 0.0F;

    const auto isComment = [](const BehaviourOutcome &o) { return o.commented; };
    const double ragebaitRate =
        rate(model, h, ragebait, hr, c, ragebaitLatent, 99, 8000, isComment);
    const double blandRate = rate(model, h, bland, hr, c, ragebaitLatent, 99, 8000, isComment);

    EXPECT_GT(ragebaitRate, blandRate + 0.2)
        << "ragebait=" << ragebaitRate << " bland=" << blandRate;
    EXPECT_GT(ragebaitRate, 0.25) << "ragebait comment rate should be clearly elevated ("
                                  << ragebaitRate << ") despite negative satisfaction";
}

// --- Profile visit driven by desire-for-similar and by creator attachment ------------------------
TEST(BehaviourModelV2Test, ProfileVisitDrivenByDesireAndCreatorAttachment) {
    const BehaviourModelV2 model = makeModel();
    const HiddenUserState h = makeUser();
    const HiddenReelState hr = makeHiddenReel();
    const Reel r = makeReel();
    const Creator neutralCreator = makeCreator(vec4(0, 1, 0, 0)); // affinity 0
    const Creator belovedCreator = makeCreator(vec4(1, 0, 0, 0)); // affinity 1 (aligned)

    const auto isVisit = [](const BehaviourOutcome &o) { return o.profileVisited; };
    const double highDesire =
        rate(model, h, r, hr, neutralCreator, makeLatent(0.0F, 0, 0, 0, 0.9F), 3, 6000, isVisit);
    const double lowDesire =
        rate(model, h, r, hr, neutralCreator, makeLatent(0.0F, 0, 0, 0, -0.9F), 3, 6000, isVisit);
    EXPECT_GT(highDesire, lowDesire + 0.1)
        << "highDesire=" << highDesire << " lowDesire=" << lowDesire;

    // Creator attachment lifts profile visits at fixed (neutral) desire.
    const LatentReaction neutralDesire = makeLatent(0.0F, 0, 0, 0, 0.0F);
    const double beloved = rate(model, h, r, hr, belovedCreator, neutralDesire, 3, 6000, isVisit);
    const double stranger = rate(model, h, r, hr, neutralCreator, neutralDesire, 3, 6000, isVisit);
    EXPECT_GT(beloved, stranger) << "beloved=" << beloved << " stranger=" << stranger;
}

// --- Not-interested fires on strongly negative satisfaction, ~never on neutral
// --------------------
TEST(BehaviourModelV2Test, NotInterestedFiresOnlyOnStronglyNegativeSatisfaction) {
    const BehaviourModelV2 model = makeModel();
    const HiddenUserState h = makeUser();
    const HiddenReelState hr = makeHiddenReel();
    const Creator c = makeCreator();
    const Reel r = makeReel();

    const auto isNI = [](const BehaviourOutcome &o) { return o.notInterested; };
    const double neutral = rate(model, h, r, hr, c, makeLatent(0.0F), 17, 6000, isNI);
    const double veryNegative = rate(model, h, r, hr, c, makeLatent(-0.9F), 17, 6000, isNI);

    EXPECT_LT(neutral, 0.01) << "neutral latent must almost never trigger not-interested ("
                             << neutral << ")";
    EXPECT_GT(veryNegative, neutral + 0.1)
        << "strongly negative satisfaction must trigger not-interested (" << veryNegative << ")";
}

// --- Emotional value drives rewatch and whole-play replays (music/comfort signal) ----------------
TEST(BehaviourModelV2Test, EmotionalValueDrivesRewatchAndReplays) {
    const BehaviourModelV2 model = makeModel();
    const HiddenUserState h = makeUser();
    const HiddenReelState hr = makeHiddenReel();
    const Creator c = makeCreator();
    const Reel r = makeReel();

    const LatentReaction highEmo = makeLatent(0.7F, /*info=*/0.0F, /*emo=*/0.95F);
    const LatentReaction lowEmo = makeLatent(0.7F, /*info=*/0.0F, /*emo=*/0.0F);

    const auto isRewatch = [](const BehaviourOutcome &o) { return o.rewatch; };
    const double highRewatch = rate(model, h, r, hr, c, highEmo, 5, 8000, isRewatch);
    const double lowRewatch = rate(model, h, r, hr, c, lowEmo, 5, 8000, isRewatch);
    EXPECT_GT(highRewatch, lowRewatch + 0.05)
        << "highEmo rewatch=" << highRewatch << " lowEmo rewatch=" << lowRewatch;

    // At least one whole-play replay (watchRatio >= 2) must appear in the high-emotion case.
    Rng rng(5);
    int replayEvents = 0;
    for (int i = 0; i < 8000; ++i) {
        if (model.sampleObservables(h, r, hr, c, highEmo, rng).replayCount >= 1) {
            ++replayEvents;
        }
    }
    EXPECT_GT(replayEvents, 0)
        << "strong emotional value should occasionally yield >=1 whole-play replay";
}

// --- Watch ratio is monotone-but-noisy in satisfaction (the Spearman wedge) ----------------------
// Package A's stub means the full mixed-population Spearman(watchRatio, immediateSatisfaction) in
// [0.2, 0.8] is a cross-package assertion (awaits A's real latent). Here we isolate the mechanism:
// over a synthetic satisfaction grid (emo held at 0), watchRatio must rise with satisfaction but
// remain clearly imperfect (the noise/band terms keep it off 1.0).
TEST(BehaviourModelV2Test, WatchRatioMonotoneButNoisyInSatisfaction) {
    const BehaviourModelV2 model = makeModel();
    const HiddenUserState h = makeUser();
    const HiddenReelState hr = makeHiddenReel();
    const Creator c = makeCreator();
    const Reel r = makeReel();

    Rng rng(2026);
    std::vector<double> sat;
    std::vector<double> watch;
    for (int step = -10; step <= 10; ++step) {
        const float s = static_cast<float>(step) / 10.0F; // -1.0 .. 1.0
        for (int i = 0; i < 400; ++i) {
            const BehaviourOutcome o = model.sampleObservables(h, r, hr, c, makeLatent(s), rng);
            sat.push_back(s);
            watch.push_back(o.watchRatio);
        }
    }
    const double rho = spearman(sat, watch);
    EXPECT_GT(rho, 0.2) << "watch<->satisfaction correlation should be clearly positive (rho="
                        << rho << ")";
    EXPECT_LT(rho, 0.98) << "... but imperfect — noise/bands keep it off 1.0 (rho=" << rho << ")";
}

// --- The untouched V1 Simulator::step path stays deterministic (D17 structural sanity) -----------
TEST(BehaviourModelV2Test, V1SimulatorStepPathIsDeterministicAndUnchanged) {
    const HiddenUserState h = makeUser();
    const Creator c = makeCreator(vec4(0.5F, 0.5F, 0, 0));
    Reel base = makeReel();

    const auto runSequence = [&](uint64_t seed) {
        Simulator sim(BehaviourConfig{}, RewardConfig{}, Rng(seed), /*recentWindow=*/8,
                      /*trendingHalfLifeSeconds=*/3600.0);
        User user{};
        user.id = UserId{1};
        Reel reel = base;
        Creator creator = c;
        std::vector<std::pair<float, InteractionType>> seq;
        for (int i = 0; i < 40; ++i) {
            const StepResult sr = sim.step(user, h, reel, creator);
            seq.emplace_back(sr.event.watchRatio, sr.event.type);
            // V1 events never carry V2 fields — they stay at their defaults (D17 gate-off shape).
            EXPECT_EQ(sr.event.positionInFeed, 0u);
            EXPECT_EQ(sr.event.requestId, 0u);
            EXPECT_FALSE(sr.event.commented);
            EXPECT_FALSE(sr.event.saved);
            EXPECT_FALSE(sr.event.profileVisited);
            EXPECT_EQ(sr.event.replayCount, 0u);
        }
        return seq;
    };

    EXPECT_EQ(runSequence(12345), runSequence(12345))
        << "the V1 step path must be byte-identical across identical seeds (untouched by the V2 "
           "additions)";
}
