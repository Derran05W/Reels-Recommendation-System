// Phase 21 Package B — RAGEBAIT AMPLIFICATION statistical test (V2 TDD Tier 4 acceptance 2, P21
// contracts §5). Reduced-scale, in-process, DETERMINISTIC (D8) event-mode runs on ONE identical
// ragebait-heavy world: an ENGAGEMENT-optimizing ranking preset vs a SATISFACTION-PROXY control,
// differing ONLY in ranking weights. Test-enforces the acceptance-2 harm signature — "engagement
// optimization should be able to create measurable negative long-term outcomes":
//
//   (1) the ragebait archetype's WHOLE-RUN exposure share (ecosystem.arch_share_whole_run.ragebait,
//       frozen §2) is HIGHER under engagement than under the proxy control; AND
//   (2) hidden welfare is WORSE under engagement — mean hidden satisfaction (welfare.mean-
//       Satisfaction, the ground-truth latent, NOT engagement reward) LOWER, OR mean final trust
//       (long_term.mean_final_trust) LOWER.
//
// The two ranking presets mirror the published scenario arms (scripts/run_phase21_b.sh /
// configs/scenarios/ragebait_amplification.json), which are VERBATIM the P20 engagement /
// satisfaction-proxy patches. The world is made ragebait-susceptible by the SAME lever the scenario
// documents: the controversyTolerance susceptibility trait is not overridable via
// realism.cohort_mix (P21 scaffold intel), so the ragebait ARCHETYPE mixture weight is raised (here
// 0.10 -> 0.30, a heavier 3x tilt than the scenario's 2x to give a clean, reproducible margin at
// 300 users / 4 days), the other seven archetypes renormalized. The SAME catalog and seed feed BOTH
// arms, so the content world is identical and only the policy differs.
//
// Margins are calibrated at the DEMONSTRATED OPERATING POINT observed from this fixture (printed
// below on every run) and set to roughly HALF the observed gap (P20 mechanism-alive-tripwire
// convention), documented at each assert. This is a directional harm tripwire, not a tight
// point-estimate bound.

#include "rr/evaluation/experiment_runner.hpp"
#include "rr/infrastructure/archetype_config.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace rr;

namespace {

namespace fs = std::filesystem;

// Catalog/evaluation index order is frozen (§2): genuinely_satisfying, useful, RAGEBAIT, clickbait,
// comfort, polished_irrelevant, niche_treasure, background_music. Guarded by an ASSERT below.
constexpr std::size_t kRagebaitIndex = 2;

// A ragebait-heavy catalog: the shipped 8-archetype catalog with ragebait's mixture weight raised
// to `ragebaitWeight` and the other seven renormalized so the catalog still sums to 1.0 (weights
// are normalized at sampling time regardless, but keeping the sum at 1.0 makes the share
// interpretable). Every other archetype parameter is untouched, so ragebait keeps its harmful
// signature (satisfaction_bias=-0.35, regret_bias=+0.35, high
// controversy/clickbait/emotional-intensity).
std::vector<ArchetypeSpec> ragebaitHeavyCatalog(double ragebaitWeight) {
    std::vector<ArchetypeSpec> cat = defaultArchetypeCatalog();
    double oldRagebait = 0.0;
    for (const ArchetypeSpec &a : cat) {
        if (a.name == "ragebait") {
            oldRagebait = a.weight;
        }
    }
    const double scale = (1.0 - ragebaitWeight) / (1.0 - oldRagebait);
    for (ArchetypeSpec &a : cat) {
        a.weight = (a.name == "ragebait") ? ragebaitWeight : a.weight * scale;
    }
    return cat;
}

// Reduced-scale event-mode base config with every P21 metric group live (welfare, session health,
// long-term/retention, ecosystem) and NO ranking preset applied yet — the two arms start from this
// identical world and differ only in the ranking weights an applyXxx() helper then sets. The
// default RankingConfig equals configs/realism-medium-retention.json's resolved ranking block (the
// scenario base), so applying the engagement / proxy patch here mirrors the scenario arms exactly.
ExperimentConfig baseWorld(uint64_t seed, double ragebaitWeight) {
    ExperimentConfig c;
    c.simulation.seed = seed;
    c.simulation.users = 300;
    c.simulation.reels = 3000;
    c.simulation.creators = 60;
    c.simulation.topics = 16;
    c.simulation.dimensions = 24; // contracts §5: dims 16-32
    c.simulation.scheduler = "event_queue";
    c.simulation.horizonSeconds = 4.0 * 86400.0; // a few simulated days
    c.recommendation.feedSize = 10;
    c.recommendation.vectorCandidates = 200;
    c.evaluation.oracleSampleRate = 0.0;
    c.evaluation.retrievalSampleRate = 0.0;
    c.evaluation.ecosystemMetrics = true; // emit the §2 ecosystem group (arch_share_whole_run)
    c.algorithm = RecommendationAlgorithm::HnswRanker;
    c.realism.contentV2 = true;
    c.realism.latentReactions = true; // welfare group (mean hidden satisfaction)
    c.realism.sessionDynamics = true; // session-health group
    c.realism.preferenceEvolution = true;
    c.retention.enabled = true; // long_term group (mean_final_trust)
    c.retention.churnDelayThresholdSeconds = 604800.0;
    c.scheduling.openStaggerSeconds = 3600.0;
    c.scheduling.returnDelayMeanSeconds = 3600.0;
    c.realism.archetypes = ragebaitHeavyCatalog(ragebaitWeight);
    return c;
}

// The P20 ENGAGEMENT preset (verbatim; see scripts/run_phase20_experiment.sh header for
// provenance).
void applyEngagement(RankingConfig &r) {
    r.durationMatchWeight = 0.0;
    r.emotionalIntensityWeight = 0.08;
    r.emotionalMatchWeight = 0.1;
    r.freshnessWeight = 0.0;
    r.impressionPenaltyWeight = 0.0;
    r.musicMatchWeight = 0.12;
    r.popularityWeight = 0.05;
    r.qualityWeight = 0.0;
    r.repetitionPenalty = 0.0;
    r.similarityWeight = 0.6;
    r.trendingWeight = 0.0;
    r.visualMatchWeight = 0.04;
}

// The P20 SATISFACTION-PROXY preset (verbatim). Weights clickbait and emotional intensity
// NEGATIVELY and rewards usefulness / production quality / information density.
void applyProxy(RankingConfig &r) {
    r.clickbaitWeight = -0.15;
    r.emotionalIntensityWeight = -0.05;
    r.informationDensityWeight = 0.06;
    r.languageMatchWeight = 0.05;
    r.productionQualityWeight = 0.08;
    r.usefulnessWeight = 0.12;
}

ExperimentResult runInto(const fs::path &root, const ExperimentConfig &cfg) {
    fs::remove_all(root);
    ExperimentRunner runner(cfg, root);
    return runner.run();
}

} // namespace

TEST(RagebaitAmplificationStatisticalTest, EngagementOverservesRagebaitAndDegradesWelfare) {
    // Index-order tripwire: archShareWholeRun[kRagebaitIndex] must be the ragebait archetype.
    ASSERT_EQ(defaultArchetypeCatalog()[kRagebaitIndex].name, "ragebait");

    constexpr double kRagebaitWeight = 0.30; // ragebait-heavy world (3x the default 0.10)
    constexpr uint64_t kSeed = 42;

    ExperimentConfig engCfg = baseWorld(kSeed, kRagebaitWeight);
    applyEngagement(engCfg.ranking);
    ExperimentConfig proxyCfg = baseWorld(kSeed, kRagebaitWeight);
    applyProxy(proxyCfg.ranking);

    const fs::path root = fs::path(::testing::TempDir()) / "rr_p21_ragebait";
    const ExperimentResult eng = runInto(root / "engagement", engCfg);
    const ExperimentResult proxy = runInto(root / "proxy", proxyCfg);

    ASSERT_TRUE(eng.ecosystem.configured);
    ASSERT_TRUE(proxy.ecosystem.configured);
    ASSERT_TRUE(eng.longTerm.configured);
    ASSERT_TRUE(proxy.longTerm.configured);

    const double engRagebait = eng.ecosystem.archShareWholeRun[kRagebaitIndex];
    const double proxyRagebait = proxy.ecosystem.archShareWholeRun[kRagebaitIndex];
    const double engSat = eng.welfare.meanSatisfaction;
    const double proxySat = proxy.welfare.meanSatisfaction;
    const double engTrust = eng.longTerm.meanFinalTrust;
    const double proxyTrust = proxy.longTerm.meanFinalTrust;

    // DEMONSTRATED OPERATING POINT (printed so the calibration is auditable / re-derivable).
    std::cout << "[ragebait op-point] ragebait_share eng=" << engRagebait
              << " proxy=" << proxyRagebait << " (gap=" << engRagebait - proxyRagebait << ")\n"
              << "                    mean_satisfaction eng=" << engSat << " proxy=" << proxySat
              << " (proxy-eng=" << proxySat - engSat << ")\n"
              << "                    mean_final_trust eng=" << engTrust << " proxy=" << proxyTrust
              << " (proxy-eng=" << proxyTrust - engTrust << ")\n";

    // DEMONSTRATED OPERATING POINT (seed 42, 300 users / 3000 reels / dim 24 / 4 days, ragebait
    // weight 0.30), observed and pinned here; every margin below is ~HALF the observed gap (P20
    // mechanism-alive-tripwire convention — a robust directional bound, not a tight point
    // estimate):
    //   ragebait share : eng 0.689 vs proxy 0.000  -> gap 0.689
    //   mean hidden sat : eng -0.475 vs proxy 0.106 -> proxy better by 0.580
    //   mean final trust: eng 0.055 vs proxy 0.172  -> proxy better by 0.117

    // (1) Engagement OVER-SERVES ragebait (the proxy control ranks its high clickbait/emotional-
    // intensity content to the bottom and surfaces ~none). Margin 0.30 is ~half the 0.689 gap.
    EXPECT_GT(engRagebait - proxyRagebait, 0.30)
        << "engagement should over-serve ragebait vs the proxy control (eng=" << engRagebait
        << ", proxy=" << proxyRagebait << ")";

    // (2) Welfare is WORSE under engagement: mean hidden satisfaction LOWER (margin 0.25 ~ half the
    // 0.580 gap) OR mean final trust LOWER (margin 0.05 ~ half the 0.117 gap). The OR mirrors
    // contracts §5; here BOTH hold, but the OR keeps the test robust to operating-point drift.
    const bool satisfactionWorse = (proxySat - engSat) > 0.25;
    const bool trustWorse = (proxyTrust - engTrust) > 0.05;
    EXPECT_TRUE(satisfactionWorse || trustWorse)
        << "engagement should degrade hidden welfare vs proxy: satisfaction eng=" << engSat
        << " proxy=" << proxySat << "; trust eng=" << engTrust << " proxy=" << proxyTrust;
}
