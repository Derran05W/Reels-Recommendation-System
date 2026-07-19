// Unit tests for the Phase 17 observables-only ToleranceEstimator (V2 TDD 4.10/5, plan task 2 +
// "estimator tests"). Drives constructed interaction histories through apply() one event at a time
// — mirroring the harness call-site contract (the event is the newest recentInteractions entry when
// apply runs) — and asserts the documented directional behaviour: sustained within-topic
// completions raise repetition tolerance; not-interested / exit after repeats lower it and raise
// topic fatigue; a declining-completion run ends below a sustained one; novel-topic completions
// raise novelty tolerance; every estimate stays bounded; and the estimator is deterministic. All
// signals are OBSERVABLE (no hidden read — the D18 guard covers that structurally).
#include "rr/learning/tolerance_estimator.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "rr/domain/creator.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/interaction.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/evaluation/cold_start.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/dataset_generator.hpp"
// EVALUATION CARVE-OUT (D18): the correlation test at the end reads HiddenUserState to SCORE the
// observables-only estimator end-to-end. This is a tests/ file (not scanned by the D18
// include-graph guard) and reads hidden state only to grade the estimate — never as an estimator
// input.
#include "rr/simulation/hidden/hidden_user_state.hpp"
#include "rr/simulation/hidden/latent_reaction.hpp"
#include "rr/simulation/simulator.hpp"

namespace {

// Reel i has dense id i, primary topic `topics[i]`, creator `creators[i]`.
std::vector<rr::Reel> makeReels(const std::vector<uint32_t> &topics,
                                const std::vector<uint32_t> &creators) {
    std::vector<rr::Reel> reels;
    reels.reserve(topics.size());
    for (std::size_t i = 0; i < topics.size(); ++i) {
        rr::Reel r{};
        r.id = rr::ReelId{static_cast<uint32_t>(i)};
        r.primaryTopic = rr::TopicId{topics[i]};
        r.creatorId = rr::CreatorId{creators[i]};
        r.active = true;
        reels.push_back(std::move(r));
    }
    return reels;
}

rr::InteractionEvent makeEvent(uint32_t reelId, uint32_t creatorId, rr::InteractionType type,
                               float watchRatio) {
    rr::InteractionEvent e{};
    e.reelId = rr::ReelId{reelId};
    e.creatorId = rr::CreatorId{creatorId};
    e.type = type;
    e.watchRatio = watchRatio;
    return e;
}

rr::DiversityConfig cfg() { return rr::DiversityConfig{}; }

constexpr std::size_t kWindow = 20; // LearningConfig.recentWindow default

// Push the event as the newest recentInteractions entry (bounding the window like the Simulator),
// then run the estimator — exactly the harness order.
void feed(const rr::ToleranceEstimator &est, rr::User &user, const std::vector<rr::Reel> &reels,
          const rr::InteractionEvent &e) {
    user.recentInteractions.push_back(e);
    while (user.recentInteractions.size() > kWindow) {
        user.recentInteractions.pop_front();
    }
    est.apply(user, reels[e.reelId.value], e);
}

float topicFatigue(const rr::User &u, uint32_t topic) {
    const auto it = u.estimatedTopicFatigue.find(rr::TopicId{topic});
    return it == u.estimatedTopicFatigue.end() ? 0.0f : it->second;
}

float creatorFatigue(const rr::User &u, uint32_t creator) {
    const auto it = u.estimatedCreatorFatigue.find(rr::CreatorId{creator});
    return it == u.estimatedCreatorFatigue.end() ? 0.0f : it->second;
}

} // namespace

// A brand-new user (no interactions) and a single first interaction both leave every estimate at
// the neutral default — no prior-window evidence yet.
TEST(ToleranceEstimatorTest, NoEvidenceStaysNeutral) {
    const std::vector<rr::Reel> reels = makeReels({1, 1}, {0, 0});
    const rr::ToleranceEstimator est(reels, cfg());
    rr::User user{};

    EXPECT_FLOAT_EQ(user.estimatedRepetitionTolerance, 0.5f);
    EXPECT_FLOAT_EQ(user.estimatedNoveltyTolerance, 0.5f);

    // One event: still no PRIOR window, so nothing moves.
    feed(est, user, reels, makeEvent(0, 0, rr::InteractionType::CompleteWatch, 1.0f));
    EXPECT_FLOAT_EQ(user.estimatedRepetitionTolerance, 0.5f);
    EXPECT_FLOAT_EQ(user.estimatedNoveltyTolerance, 0.5f);
    EXPECT_TRUE(user.estimatedTopicFatigue.empty());
    EXPECT_TRUE(user.estimatedCreatorFatigue.empty());
}

// Repeated within-topic completions => "repetition doesn't bother them" => repetition tolerance
// climbs well above neutral, while novelty tolerance (no novel items) barely moves.
TEST(ToleranceEstimatorTest, SustainedWithinTopicCompletionsRaiseRepetitionTolerance) {
    std::vector<uint32_t> topics(15, 1); // all same topic
    std::vector<uint32_t> creators(15);
    for (uint32_t i = 0; i < 15; ++i) {
        creators[i] = i; // distinct creators, so this is TOPIC repetition, not creator repetition
    }
    const std::vector<rr::Reel> reels = makeReels(topics, creators);
    const rr::ToleranceEstimator est(reels, cfg());
    rr::User user{};
    for (uint32_t i = 0; i < 15; ++i) {
        feed(est, user, reels, makeEvent(i, i, rr::InteractionType::CompleteWatch, 1.0f));
    }
    EXPECT_GT(user.estimatedRepetitionTolerance, 0.75f);
    // No topic fatigue accrues from completions (a completed repeat is evidence of NOT being
    // tired).
    EXPECT_LT(topicFatigue(user, 1), 0.1f);
}

// Not-interested after a within-topic run drops repetition tolerance and spikes THAT topic's
// fatigue (plan task (a)/(b)).
TEST(ToleranceEstimatorTest, NotInterestedAfterRepeatLowersToleranceAndRaisesTopicFatigue) {
    std::vector<uint32_t> topics(12, 7);
    std::vector<uint32_t> creators(12, 3);
    const std::vector<rr::Reel> reels = makeReels(topics, creators);
    const rr::ToleranceEstimator est(reels, cfg());
    rr::User user{};
    // Six same-topic completions build tolerance up and repetition context high...
    for (uint32_t i = 0; i < 6; ++i) {
        feed(est, user, reels, makeEvent(i, 3, rr::InteractionType::CompleteWatch, 1.0f));
    }
    const float afterCompletions = user.estimatedRepetitionTolerance;
    EXPECT_GT(afterCompletions, 0.5f);
    // ...then several not-interested on the SAME topic pull it back down and raise topic fatigue.
    for (uint32_t i = 6; i < 12; ++i) {
        feed(est, user, reels, makeEvent(i, 3, rr::InteractionType::NotInterested, 0.0f));
    }
    EXPECT_LT(user.estimatedRepetitionTolerance, afterCompletions);
    EXPECT_LT(user.estimatedRepetitionTolerance, 0.5f);
    EXPECT_GT(topicFatigue(user, 7), 0.3f);
    // The repeated same-creator negatives also raise creator fatigue.
    EXPECT_GT(creatorFatigue(user, 3), 0.3f);
}

// An observed session exit after a repetitive window is a strong intolerance signal: the same run
// ending in an exit-after-impression lands LOWER on repetition tolerance than without the exit.
TEST(ToleranceEstimatorTest, ExitAfterRepetitionLowersRepetitionTolerance) {
    std::vector<uint32_t> topics(10, 2);
    std::vector<uint32_t> creators(10, 1);
    const std::vector<rr::Reel> reels = makeReels(topics, creators);
    const rr::ToleranceEstimator est(reels, cfg());

    auto runRepetitiveRun = [&](bool exitOnLast) {
        rr::User user{};
        for (uint32_t i = 0; i < 10; ++i) {
            rr::InteractionEvent e = makeEvent(i, 1, rr::InteractionType::PartialWatch, 0.5f);
            if (exitOnLast && i == 9) {
                e.observedExitAfterImpression = true;
            }
            feed(est, user, reels, e);
        }
        return user.estimatedRepetitionTolerance;
    };

    const float withExit = runRepetitiveRun(true);
    const float withoutExit = runRepetitiveRun(false);
    EXPECT_LT(withExit, withoutExit);
}

// A run whose completions DECLINE (completions then skips, all same topic) ends with LOWER
// repetition tolerance than a fully-sustained completion run of the same length (plan task (a)).
TEST(ToleranceEstimatorTest, DecliningCompletionRunLowersToleranceVsSustained) {
    std::vector<uint32_t> topics(14, 5);
    std::vector<uint32_t> creators(14, 2);
    const std::vector<rr::Reel> reels = makeReels(topics, creators);
    const rr::ToleranceEstimator est(reels, cfg());

    rr::User sustained{};
    for (uint32_t i = 0; i < 14; ++i) {
        feed(est, sustained, reels, makeEvent(i, 2, rr::InteractionType::CompleteWatch, 1.0f));
    }

    rr::User declining{};
    for (uint32_t i = 0; i < 7; ++i) {
        feed(est, declining, reels, makeEvent(i, 2, rr::InteractionType::CompleteWatch, 1.0f));
    }
    for (uint32_t i = 7; i < 14; ++i) {
        feed(est, declining, reels, makeEvent(i, 2, rr::InteractionType::InstantSkip, 0.0f));
    }

    EXPECT_LT(declining.estimatedRepetitionTolerance, sustained.estimatedRepetitionTolerance);
}

// Completing NOVEL-topic items (every topic fresh) raises novelty tolerance while leaving
// repetition tolerance untouched; skipping novel items lowers it.
TEST(ToleranceEstimatorTest, NovelCompletionRaisesNoveltyToleranceSkippingLowersIt) {
    std::vector<uint32_t> topics(15);
    std::vector<uint32_t> creators(15);
    for (uint32_t i = 0; i < 15; ++i) {
        topics[i] = i + 1; // every topic distinct => each item is novel to the window
        creators[i] = i + 1;
    }
    const std::vector<rr::Reel> reels = makeReels(topics, creators);
    const rr::ToleranceEstimator est(reels, cfg());

    rr::User embraces{};
    for (uint32_t i = 0; i < 15; ++i) {
        feed(est, embraces, reels, makeEvent(i, i + 1, rr::InteractionType::CompleteWatch, 1.0f));
    }
    EXPECT_GT(embraces.estimatedNoveltyTolerance, 0.75f);
    // No topic repetition anywhere => repetition tolerance had no evidence and stayed neutral.
    EXPECT_FLOAT_EQ(embraces.estimatedRepetitionTolerance, 0.5f);

    rr::User rejects{};
    for (uint32_t i = 0; i < 15; ++i) {
        feed(est, rejects, reels, makeEvent(i, i + 1, rr::InteractionType::InstantSkip, 0.0f));
    }
    EXPECT_LT(rejects.estimatedNoveltyTolerance, 0.25f);
}

// Comment / save / profile-visit cadence is mild POSITIVE engagement evidence on the current topic:
// a repetitive run of PartialWatch (sat 0.5) that ALSO carries save/comment flags lands higher on
// repetition tolerance than the same run without them (the flags lift sat to the positive extreme).
TEST(ToleranceEstimatorTest, EngagementCadenceIsPositiveEvidence) {
    std::vector<uint32_t> topics(10, 4);
    std::vector<uint32_t> creators(10, 6);
    const std::vector<rr::Reel> reels = makeReels(topics, creators);
    const rr::ToleranceEstimator est(reels, cfg());

    auto run = [&](bool engaged) {
        rr::User user{};
        for (uint32_t i = 0; i < 10; ++i) {
            rr::InteractionEvent e = makeEvent(i, 6, rr::InteractionType::PartialWatch, 0.5f);
            if (engaged) {
                e.saved = true;
                e.commented = true;
            }
            feed(est, user, reels, e);
        }
        return user.estimatedRepetitionTolerance;
    };

    EXPECT_GT(run(true), run(false));
}

// A topic the user was fatigued of RECOVERS once it stops recurring: it decays every event and is
// eventually PRUNED to absent (the documented "decays otherwise"). The decay is gradual (a ~13.5-
// interaction half-life), so recovery to negligible takes many interactions — asserted honestly:
// it more-than-halves quickly, and full pruning is reached over a long run of unrelated content.
TEST(ToleranceEstimatorTest, TopicFatigueDecaysWhenTopicStopsRecurring) {
    // 90 reels: first 6 on topic 9 (build fatigue via not-interested), rest on distinct fresh
    // topics (so topic 9 never recurs and decays out of the maps).
    std::vector<uint32_t> topics(90);
    std::vector<uint32_t> creators(90, 8);
    for (uint32_t i = 0; i < 90; ++i) {
        topics[i] = (i < 6) ? 9 : 100 + i; // topic 9 only for the first six
    }
    const std::vector<rr::Reel> reels = makeReels(topics, creators);
    const rr::ToleranceEstimator est(reels, cfg());
    rr::User user{};
    for (uint32_t i = 0; i < 6; ++i) {
        feed(est, user, reels, makeEvent(i, 8, rr::InteractionType::NotInterested, 0.0f));
    }
    const float builtUp = topicFatigue(user, 9);
    EXPECT_GT(builtUp, 0.2f);

    // A run of unrelated content strictly decays topic 9's fatigue (gradual recovery)...
    for (uint32_t i = 6; i < 26; ++i) {
        feed(est, user, reels, makeEvent(i, 8, rr::InteractionType::CompleteWatch, 1.0f));
    }
    EXPECT_LT(topicFatigue(user, 9), builtUp)
        << "fatigue must decay once the topic stops recurring";
    EXPECT_LT(topicFatigue(user, 9), 0.35f);

    // ...and a long enough run eventually decays it below the prune epsilon => erased/absent.
    for (uint32_t i = 26; i < 90; ++i) {
        feed(est, user, reels, makeEvent(i, 8, rr::InteractionType::CompleteWatch, 1.0f));
    }
    EXPECT_EQ(user.estimatedTopicFatigue.count(rr::TopicId{9}), 0u) << "fatigue should be pruned";
}

// Every estimate stays in [0,1] under a long adversarial stream mixing extreme signals.
TEST(ToleranceEstimatorTest, EstimatesStayBounded) {
    std::vector<uint32_t> topics(60);
    std::vector<uint32_t> creators(60);
    for (uint32_t i = 0; i < 60; ++i) {
        topics[i] = i % 3;   // heavy topic repetition
        creators[i] = i % 2; // heavy creator repetition
    }
    const std::vector<rr::Reel> reels = makeReels(topics, creators);
    const rr::ToleranceEstimator est(reels, cfg());
    rr::User user{};
    const rr::InteractionType types[] = {
        rr::InteractionType::CompleteWatch, rr::InteractionType::NotInterested,
        rr::InteractionType::Like,          rr::InteractionType::InstantSkip,
        rr::InteractionType::Rewatch,       rr::InteractionType::PartialWatch};
    for (uint32_t i = 0; i < 60; ++i) {
        rr::InteractionEvent e = makeEvent(i, i % 2, types[i % 6], (i % 6) / 5.0f);
        e.observedExitAfterImpression = (i % 7 == 0);
        e.saved = (i % 5 == 0);
        feed(est, user, reels, e);
        EXPECT_GE(user.estimatedRepetitionTolerance, 0.0f);
        EXPECT_LE(user.estimatedRepetitionTolerance, 1.0f);
        EXPECT_GE(user.estimatedNoveltyTolerance, 0.0f);
        EXPECT_LE(user.estimatedNoveltyTolerance, 1.0f);
    }
    for (const auto &[topic, f] : user.estimatedTopicFatigue) {
        EXPECT_GE(f, 0.0f);
        EXPECT_LE(f, 1.0f);
    }
    for (const auto &[creator, f] : user.estimatedCreatorFatigue) {
        EXPECT_GE(f, 0.0f);
        EXPECT_LE(f, 1.0f);
    }
}

// Same event sequence => identical estimates (rng/clock-free pure function).
TEST(ToleranceEstimatorTest, Deterministic) {
    std::vector<uint32_t> topics(20);
    std::vector<uint32_t> creators(20);
    for (uint32_t i = 0; i < 20; ++i) {
        topics[i] = i % 4;
        creators[i] = i % 3;
    }
    const std::vector<rr::Reel> reels = makeReels(topics, creators);
    const rr::ToleranceEstimator est(reels, cfg());

    auto run = [&]() {
        rr::User user{};
        for (uint32_t i = 0; i < 20; ++i) {
            const auto type = (i % 3 == 0) ? rr::InteractionType::InstantSkip
                                           : rr::InteractionType::CompleteWatch;
            feed(est, user, reels, makeEvent(i, i % 3, type, 0.7f));
        }
        return user;
    };
    const rr::User a = run();
    const rr::User b = run();
    EXPECT_FLOAT_EQ(a.estimatedRepetitionTolerance, b.estimatedRepetitionTolerance);
    EXPECT_FLOAT_EQ(a.estimatedNoveltyTolerance, b.estimatedNoveltyTolerance);
    EXPECT_EQ(a.estimatedTopicFatigue.size(), b.estimatedTopicFatigue.size());
    for (const auto &[topic, f] : a.estimatedTopicFatigue) {
        ASSERT_EQ(b.estimatedTopicFatigue.count(topic), 1u);
        EXPECT_FLOAT_EQ(b.estimatedTopicFatigue.at(topic), f);
    }
}

// ============================================================================================
//  EVALUATION-ONLY CORRELATION CHECK (plan task 4). NOT a training signal — the estimator is
//  observables-only (the D18 guard covers that structurally); this test reads HiddenUserState
//  purely to SCORE the estimate end-to-end, and lives in tests/ (which the guard does not scan).
// ============================================================================================
namespace {

double pearson(const std::vector<double> &x, const std::vector<double> &y) {
    const std::size_t n = x.size();
    double mx = 0.0;
    double my = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        mx += x[i];
        my += y[i];
    }
    mx /= static_cast<double>(n);
    my /= static_cast<double>(n);
    double sxy = 0.0;
    double sxx = 0.0;
    double syy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double dx = x[i] - mx;
        const double dy = y[i] - my;
        sxy += dx * dy;
        sxx += dx * dx;
        syy += dy * dy;
    }
    return (sxx > 0.0 && syy > 0.0) ? sxy / std::sqrt(sxx * syy) : 0.0;
}

} // namespace

// Drives a reduced gate-on simulation (the SAME public APIs ExperimentRunner uses — generateDataset
// + the P16 Simulator + ToleranceEstimator, forked exactly as the harness forks) over deliberately
// repetition-heavy single-topic streams, then correlates the recommender-visible
// estimatedRepetitionTolerance against the hidden repetitionTolerance across the population. Under
// repetition the hidden trait modulates fatigue (Package A's wiring): tolerant users keep
// completing while intolerant users skip / exit sooner, so the observables-only estimate should
// track the hidden trait with a positive (if noisy) correlation. (ExperimentRunner is not called
// directly because its ExperimentResult does not surface per-user estimates/hidden traits — a
// documented deviation from the plan's "via ExperimentRunner" wording; the loop below replays its
// core.)
TEST(ToleranceEstimatorTest, EstimateCorrelatesWithHiddenRepetitionTolerance) {
    constexpr double kCorrelationFloor = 0.15; // modest floor (estimates are noisy); named constant

    rr::SimulationConfig sc;
    sc.users = 150;
    sc.reels = 2400;
    sc.creators = 150;
    sc.topics = 8;      // few topics => many reels/topic for long single-topic streams
    sc.dimensions = 16; // keep the fixture fast
    sc.interactionsPerUser = 40;
    rr::RealismConfig realism;
    realism.contentV2 = true; // samples the hidden repetitionTolerance trait
    realism.latentReactions = true;
    realism.sessionDynamics = true;       // Package A wires repetitionTolerance into fatigue here
    realism.personalizedDiversity = true; // the gate the estimator runs under

    const uint64_t seed = 20260718;
    rr::GeneratedDataset ds = rr::generateDataset(sc, realism, seed);
    rr::applyColdStart(ds.users, rr::globalAveragePreference(ds.hiddenStates));

    // Index reels by primary topic so each user gets a distinct-reel, SAME-TOPIC stream (maximal
    // repetition context => the strongest possible observable signal about repetition tolerance).
    std::vector<std::vector<uint32_t>> reelsByTopic(sc.topics);
    for (const rr::Reel &r : ds.reels) {
        reelsByTopic[r.primaryTopic.value].push_back(r.id.value);
    }

    rr::Simulator sim(rr::BehaviourConfig{}, rr::BehaviourV2Config{}, rr::SessionDynamicsConfig{},
                      rr::RewardConfig{}, rr::forkRng(seed, "behaviour"),
                      rr::forkRng(seed, "satisfaction"), rr::forkRng(seed, "session-exit"),
                      rr::forkRng(seed, "external-interruption"), /*recentWindow=*/20,
                      /*trendingHalfLifeSeconds=*/3600.0);
    const rr::ToleranceEstimator est(ds.reels, rr::DiversityConfig{});

    const std::size_t perUser = 40;
    uint64_t reqId = 0;
    for (std::size_t u = 0; u < ds.users.size(); ++u) {
        rr::User &user = ds.users[u];
        const std::vector<uint32_t> &pool = reelsByTopic[u % sc.topics];
        if (pool.size() < perUser) {
            continue; // defensive: not enough distinct same-topic reels (won't trigger here)
        }
        for (std::size_t i = 0; i < perUser; ++i) {
            const uint32_t reelId = pool[(u * 7 + i) % pool.size()]; // distinct within this user
            rr::Reel &reel = ds.reels[reelId];
            const rr::Creator &creator = ds.creators[reel.creatorId.value];
            rr::StepV2Inputs v2;
            v2.hiddenReel = &ds.hiddenReelStates[reelId];
            v2.positionInFeed = static_cast<uint32_t>(i % 10);
            v2.requestId = ++reqId;
            v2.requestTimestamp = sim.now();
            rr::LatentReaction latent;
            rr::SessionRecord closed{};
            const rr::StepResult sr =
                sim.stepV2(user, ds.hiddenStates[u], reel, creator, v2, latent, &closed);
            est.apply(user, reel, sr.event); // OBSERVABLE event only
        }
    }

    std::vector<double> estimated;
    std::vector<double> hidden;
    estimated.reserve(ds.users.size());
    hidden.reserve(ds.users.size());
    for (std::size_t u = 0; u < ds.users.size(); ++u) {
        estimated.push_back(static_cast<double>(ds.users[u].estimatedRepetitionTolerance));
        hidden.push_back(static_cast<double>(ds.hiddenStates[u].repetitionTolerance));
    }
    const double r = pearson(estimated, hidden);
    std::cout << "[p17-correlation] Pearson r(estimatedRepetitionTolerance, "
                 "hiddenRepetitionTolerance) = "
              << r << " over " << ds.users.size() << " users (floor " << kCorrelationFloor << ")\n";
    EXPECT_GT(r, kCorrelationFloor)
        << "observables-only estimate should correlate with the hidden repetition-tolerance trait";
}
