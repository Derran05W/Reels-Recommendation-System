// Phase 20 gate-off byte-identity + stream-discipline property suite (contracts §7 A-row test j;
// D17/D19), mirroring the P13 v2_stream_discipline pattern one level up — at the RUNNER, because
// PreferenceEvolution acts inside the simulation loop (applyImpression after each impression), not
// at dataset generation. The proofs:
//   * GATE-OFF DETERMINISM (24 seeds): preference_evolution off, same seed twice => bit-identical
//     ExperimentResult.
//   * GATE-OFF BYTE-IDENTITY vs evolution config (24 seeds): with the gate off, the
//   evolution.eta_evo
//     knob is INERT — a run at eta_evo=0.02 is byte-identical to eta_evo=0.5. This is the in-tree
//     evidence that A's component is fully gated (the committed goldens are the integrator's
//     check).
//   * ZERO NEW DRAWS, STRUCTURAL: applyImpression takes no Rng and draws nothing (the "preference-
//     evolution" stream stays reserved-unused, D19); the exposure modulation in BehaviourModelV2 is
//     pure arithmetic guarded on hidden.exposure.initialized (false under gate-off). So gate-on's
//     V1 stream draw sequences are unperturbed by the component itself.
//   * GATE-ON DETERMINISM (24 seeds): preference_evolution on, same seed twice => bit-identical.
//   * GATE IS LIVE: turning the gate on measurably shifts the hidden-preference-derived metric.
//   * DRIFT + EVOLUTION COEXISTENCE (test h): drift retargets the preference exogenously, evolution
//     keeps operating on the retargeted vector, and the composite is bit-identical across two runs.
//
// Statistical margins: none here — every assertion is exact determinism/identity.

#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/simulation/dataset_generator.hpp"
#include "rr/simulation/drift_scheduler.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"
#include "rr/simulation/preference_evolution.hpp"

using namespace rr;

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kNumSeeds = 24; // >= 20 (D17 property-test requirement)

// Small full-gate-stack round-robin config (content_v2 -> latent_reactions -> session_dynamics, the
// stack preference_evolution requires). Round-robin is the D17 golden path.
ExperimentConfig baseConfig(uint64_t seed) {
    ExperimentConfig c;
    c.simulation.seed = seed;
    c.simulation.users = 20;
    c.simulation.reels = 48;
    c.simulation.creators = 12;
    c.simulation.topics = 8;
    c.simulation.dimensions = 16;
    c.simulation.interactionsPerUser = 16;
    c.recommendation.feedSize = 5;
    c.recommendation.vectorCandidates = 40;
    c.evaluation.oracleSampleRate = 0.0;
    c.evaluation.retrievalSampleRate = 0.0;
    c.algorithm = RecommendationAlgorithm::HnswRanker;
    c.realism.contentV2 = true;
    c.realism.latentReactions = true;
    c.realism.sessionDynamics = true;
    return c;
}

// Run into a fresh unique temp dir; the in-memory result is all we compare (no file reads).
ExperimentResult runInto(const ExperimentConfig &cfg, const fs::path &root) {
    fs::remove_all(root);
    ExperimentRunner runner(cfg, root);
    return runner.run();
}

fs::path tmpRoot(const std::string &tag) {
    return fs::temp_directory_path() / ("rr_p20_evo_" + tag);
}

// Bit-identity over the deterministic ExperimentResult surface (EXPECT_EQ on doubles == same bits).
// meanTrueAffinity is hidden-preference-derived (§18.2 carve-out), so it is the sensitive witness
// of whether evolution moved the hidden state.
void expectIdentical(const ExperimentResult &a, const ExperimentResult &b) {
    EXPECT_EQ(a.impressionCount, b.impressionCount);
    EXPECT_EQ(a.requestCount, b.requestCount);
    EXPECT_EQ(a.overall.impressions, b.overall.impressions);
    EXPECT_EQ(a.overall.completionRate, b.overall.completionRate);
    EXPECT_EQ(a.overall.meanWatchSeconds, b.overall.meanWatchSeconds);
    EXPECT_EQ(a.overall.instantSkipRate, b.overall.instantSkipRate);
    EXPECT_EQ(a.overall.likeRate, b.overall.likeRate);
    EXPECT_EQ(a.overall.commentRate, b.overall.commentRate);
    EXPECT_EQ(a.overall.saveRate, b.overall.saveRate);
    EXPECT_EQ(a.overall.meanTrueAffinity, b.overall.meanTrueAffinity);
    EXPECT_EQ(a.overall.rewardPerImpression, b.overall.rewardPerImpression);
    EXPECT_EQ(a.cumulativeRegret, b.cumulativeRegret);
    EXPECT_EQ(a.finalEstimatedHiddenCosine, b.finalEstimatedHiddenCosine);
}

} // namespace

// ============================================================================================
//  GATE-OFF DETERMINISM: preference_evolution off, same seed twice => bit-identical.
// ============================================================================================
TEST(EvolutionStreamDiscipline, GateOffSameSeedIsBitIdentical) {
    const fs::path root = tmpRoot("gateoff_det");
    for (uint64_t seed = 1; seed <= kNumSeeds; ++seed) {
        SCOPED_TRACE("seed=" + std::to_string(seed));
        ExperimentConfig c = baseConfig(seed); // preference_evolution stays default-false
        const ExperimentResult a = runInto(c, root / "a");
        const ExperimentResult b = runInto(c, root / "b");
        expectIdentical(a, b);
    }
    fs::remove_all(root);
}

// ============================================================================================
//  GATE-OFF BYTE-IDENTITY: with the gate off, evolution.eta_evo is INERT. A wildly different rate
//  produces byte-identical output — the component is fully gated (nothing runs under gate-off).
// ============================================================================================
TEST(EvolutionStreamDiscipline, GateOffIsUnaffectedByEvolutionConfig) {
    const fs::path root = tmpRoot("gateoff_cfg");
    for (uint64_t seed = 1; seed <= kNumSeeds; ++seed) {
        SCOPED_TRACE("seed=" + std::to_string(seed));
        ExperimentConfig lo = baseConfig(seed);
        lo.realism.preferenceEvolution = false;
        lo.evolution.etaEvo = 0.02;
        ExperimentConfig hi = baseConfig(seed);
        hi.realism.preferenceEvolution = false;
        hi.evolution.etaEvo = 0.5; // 25x — irrelevant while the gate is off
        expectIdentical(runInto(lo, root / "lo"), runInto(hi, root / "hi"));
    }
    fs::remove_all(root);
}

// ============================================================================================
//  GATE-ON DETERMINISM: preference_evolution on (A's component active), same seed twice =>
//  identical.
// ============================================================================================
TEST(EvolutionStreamDiscipline, GateOnSameSeedIsBitIdentical) {
    const fs::path root = tmpRoot("gateon_det");
    for (uint64_t seed = 1; seed <= kNumSeeds; ++seed) {
        SCOPED_TRACE("seed=" + std::to_string(seed));
        ExperimentConfig c = baseConfig(seed);
        c.realism.preferenceEvolution = true;
        c.evolution.etaEvo = 0.1;
        expectIdentical(runInto(c, root / "a"), runInto(c, root / "b"));
    }
    fs::remove_all(root);
}

// ============================================================================================
//  GATE IS LIVE: turning preference_evolution on shifts the hidden-preference-derived metric, so
//  the component is actually mutating the world (and its effect is isolated to the gate).
// ============================================================================================
TEST(EvolutionStreamDiscipline, GateOnShiftsHiddenPreferenceMetric) {
    const fs::path root = tmpRoot("gate_live");
    uint32_t differed = 0;
    for (uint64_t seed = 1; seed <= kNumSeeds; ++seed) {
        ExperimentConfig off = baseConfig(seed);
        ExperimentConfig on = baseConfig(seed);
        on.realism.preferenceEvolution = true;
        on.evolution.etaEvo = 0.3; // strong so the shift clears floating-point noise
        const ExperimentResult ro = runInto(off, root / "off");
        const ExperimentResult rn = runInto(on, root / "on");
        // The component itself draws ZERO rng, but it MUTATES hidden preferences, so the hidden-
        // preference-derived true-affinity metric moves. (Impression counts may ALSO differ,
        // because evolved satisfaction feeds the P16 probabilistic session exit — that is the
        // feature shaping the world, not a stream perturbation by the component; so counts are NOT
        // asserted equal here.)
        if (ro.overall.meanTrueAffinity != rn.overall.meanTrueAffinity) {
            ++differed;
        }
    }
    fs::remove_all(root);
    EXPECT_GT(differed, kNumSeeds / 2); // the gate demonstrably changes the hidden world
}

// ============================================================================================
//  (h) DRIFT + EVOLUTION COEXISTENCE: drift retargets the hidden preference exogenously at its
//  interaction mark; evolution keeps reinforcing the RETARGETED vector afterwards. Same inputs
//  twice
//  => bit-identical hidden preferences (both are rng-free/deterministic). Driven as a manual
//  per-impression loop so the hidden preference itself is inspected directly.
// ============================================================================================
TEST(EvolutionStreamDiscipline, DriftAndEvolutionCoexistDeterministically) {
    SimulationConfig sc;
    sc.users = 8;
    sc.reels = 40;
    sc.creators = 8;
    sc.topics = 8;
    sc.dimensions = 16;
    RealismConfig realism;
    realism.contentV2 = true; // populate modality channels
    const GeneratedDataset ds = generateDataset(sc, realism, /*seed=*/7);

    // Drift the whole population to topic 5 at interaction 3 (the EXACT legacy mechanism, keyed on
    // completed-interaction count, applied BEFORE the step — how both runners sequence it).
    DriftEvent ev;
    ev.atInteraction = 3;
    ev.cohortLo = 0.0;
    ev.cohortHi = 1.0;
    ev.topicMix = {DriftTopicWeight{5, 1.0}};
    const DriftScheduler drift(DriftConfig{{ev}}, ds.topics);
    const Embedding driftTarget = ds.topics[5].centre; // normalize(1.0 * centre) == centre

    EvolutionConfig ecfg;
    ecfg.etaEvo = 0.2;

    // One deterministic run of the drift-before / evolution-after loop for a single user.
    // `withDrift` toggles the exogenous retarget so a no-drift counterfactual isolates drift's
    // contribution.
    const uint32_t kInteractions = 8;
    auto runUser = [&](bool withDrift, std::vector<Embedding> &prefTrace) {
        PreferenceEvolution evo(ecfg);
        HiddenUserState h = ds.hiddenStates[0];
        for (uint32_t i = 0; i < kInteractions; ++i) {
            if (withDrift) {
                drift.maybeApply(h,
                                 i); // BEFORE the step (exogenous retarget at i == atInteraction)
            }
            const Reel &reel = ds.reels[i % ds.reels.size()];
            // Deterministic synthetic latent (no rng): satisfaction = current true affinity, so
            // evolution reinforces toward whatever the (possibly drifted) preference already likes.
            LatentReaction latent{};
            latent.immediateSatisfaction =
                std::clamp(dot(h.hiddenPreference, reel.embedding), -1.0F, 1.0F);
            evo.applyImpression(h, reel, latent, /*now=*/1000 + i);
            prefTrace.push_back(h.hiddenPreference);
        }
    };

    std::vector<Embedding> traceA;
    std::vector<Embedding> traceB;
    std::vector<Embedding> traceNoDrift;
    runUser(/*withDrift=*/true, traceA);
    runUser(/*withDrift=*/true, traceB);
    runUser(/*withDrift=*/false, traceNoDrift);

    // Deterministic (test h): bit-identical hidden preferences across the two drift+evolution runs.
    ASSERT_EQ(traceA.size(), traceB.size());
    for (std::size_t i = 0; i < traceA.size(); ++i) {
        EXPECT_EQ(traceA[i], traceB[i]);
    }

    // Coexistence: drift altered the EVOLVED trajectory (final preference differs from the no-drift
    // counterfactual), and pulled the preference toward topic 5 — the drift run ends up MORE
    // topic-5-aligned than the no-drift run even after evolution kept reinforcing afterwards.
    EXPECT_NE(traceA.back(), traceNoDrift.back());
    EXPECT_GT(dot(traceA.back(), driftTarget), dot(traceNoDrift.back(), driftTarget));
    // Evolution kept operating ON the retargeted vector: still a unit vector, moved OFF the exact
    // drift target (not frozen at it) — the two mechanisms compose rather than one clobbering.
    EXPECT_TRUE(isValid(traceA.back()));
    EXPECT_LT(dot(traceA.back(), driftTarget), 1.0F);
}
