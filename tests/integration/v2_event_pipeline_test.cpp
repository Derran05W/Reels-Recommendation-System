// Realism V2 event-pipeline integration test (Phase 14, V2 TDD 4.3/5). Exercises the whole
// observable path with the gates ON: computeLatentReaction (package A's neutral stub in this
// tree) -> BehaviourModelV2 observable sampling -> Simulator::stepV2 event assembly + counters ->
// welfare accumulation -> to_json round-trip, plus the gate-OFF byte-identity shape and same-seed
// determinism. Cross-package numeric signatures (satisfaction/regret magnitudes) await package A's
// real latent; here we assert the plumbing is coherent, populated, and deterministic.

#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

#include "rr/domain/interaction.hpp"
#include "rr/domain/serialization.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/dataset_generator.hpp"
#include "rr/simulation/simulator.hpp"

using namespace rr;

namespace {

namespace fs = std::filesystem;

// Smoke-scale gate-on config: content_v2 + latent_reactions on, a couple of rounds.
ExperimentConfig v2Config(bool latentReactions) {
    ExperimentConfig c;
    c.simulation.seed = 2026;
    c.simulation.users = 100;
    c.simulation.reels = 1000;
    c.simulation.creators = 20;
    c.simulation.topics = 8;
    c.simulation.dimensions = 16;
    c.simulation.interactionsPerUser = 10;
    c.recommendation.feedSize = 5; // ceil(10/5) = 2 rounds
    c.recommendation.vectorCandidates = 100;
    c.evaluation.oracleSampleRate = 0.1;
    c.algorithm = RecommendationAlgorithm::HnswRanker;
    c.realism.contentV2 = latentReactions; // latent_reactions requires content_v2 (D17)
    c.realism.latentReactions = latentReactions;
    return c;
}

// Deterministic fingerprint of the parts of an ExperimentResult that MUST be reproducible (no
// wall-clock, no experiment-id/timestamp).
std::string fingerprint(const ExperimentResult &r) {
    nlohmann::json j;
    j["impressions"] = r.impressionCount;
    j["requests"] = r.requestCount;
    j["mean_watch_ratio"] = r.overall.meanWatchRatio;
    j["completion_rate"] = r.overall.completionRate;
    j["like_rate"] = r.overall.likeRate;
    j["reward_per_impression"] = r.overall.rewardPerImpression;
    j["mean_true_affinity"] = r.overall.meanTrueAffinity;
    j["welfare_impressions"] = r.welfare.impressions;
    j["welfare_mean_satisfaction"] = r.welfare.meanSatisfaction;
    j["welfare_mean_regret"] = r.welfare.meanRegret;
    nlohmann::json rounds = nlohmann::json::array();
    for (const WelfareRoundPoint &p : r.welfare.byRound) {
        rounds.push_back({p.round, p.impressions, p.meanSatisfaction, p.meanRegret});
    }
    j["welfare_rounds"] = rounds;
    return j.dump();
}

} // namespace

// --- Directly drive stepV2 over a generated V2 dataset: events coherent, V2 flags appear ---------
TEST(V2EventPipelineTest, StepV2EventsAreCoherentAndV2FlagsAppear) {
    const ExperimentConfig cfg = v2Config(/*latentReactions=*/true);
    const uint64_t seed = cfg.simulation.seed;
    GeneratedDataset ds = generateDataset(cfg.simulation, cfg.realism, seed);
    ASSERT_EQ(ds.hiddenReelStates.size(), ds.reels.size())
        << "content_v2 must populate hidden reel states index-aligned with reels";

    Simulator sim(cfg.behaviour, cfg.behaviourV2, cfg.reward, forkRng(seed, "behaviour"),
                  forkRng(seed, "satisfaction"), cfg.learning.recentWindow,
                  cfg.ranking.trendingHalfLifeSeconds);

    const size_t feedSize = cfg.recommendation.feedSize;
    const size_t rounds = 2;
    const size_t reelCount = ds.reels.size();

    std::vector<InteractionEvent> events;
    uint64_t requestId = 0;
    for (size_t round = 0; round < rounds; ++round) {
        for (size_t u = 0; u < ds.users.size(); ++u) {
            const Timestamp requestTime = sim.now(); // one request time for the whole feed
            ++requestId;
            for (size_t k = 0; k < feedSize; ++k) {
                const size_t idx = (u * 101 + round * feedSize + k) % reelCount;
                Reel &reel = ds.reels[idx];
                const Creator &creator = ds.creators[reel.creatorId.value];
                StepV2Inputs v2;
                v2.hiddenReel = &ds.hiddenReelStates[idx];
                v2.positionInFeed = static_cast<uint32_t>(k);
                v2.requestId = requestId;
                v2.requestTimestamp = requestTime;
                v2.fromExploration = false;
                v2.sourceProvenance = CandidateSource::VectorHNSW;
                LatentReaction latent;
                const StepResult sr =
                    sim.stepV2(ds.users[u], ds.hiddenStates[u], reel, creator, v2, latent);
                events.push_back(sr.event);
            }
        }
    }

    ASSERT_FALSE(events.empty());
    size_t comments = 0;
    size_t saves = 0;
    size_t profileVisits = 0;
    for (const InteractionEvent &e : events) {
        EXPECT_LT(e.positionInFeed, feedSize);
        EXPECT_GT(e.requestId, 0u);
        // requestTimestamp <= startTimestamp <= finishTimestamp <= timestamp (finish + overhead).
        EXPECT_LE(e.requestTimestamp, e.startTimestamp);
        EXPECT_LE(e.startTimestamp, e.finishTimestamp);
        EXPECT_LE(e.finishTimestamp, e.timestamp);
        // dwell covers at least the watched seconds.
        EXPECT_GE(e.dwellSeconds, e.watchSeconds - 1e-3F);
        comments += e.commented ? 1 : 0;
        saves += e.saved ? 1 : 0;
        profileVisits += e.profileVisited ? 1 : 0;
    }
    // Even with A's neutral latent, the base propensities + reel-attribute-driven terms fire.
    EXPECT_GT(comments, 0u) << "no comments fired across " << events.size() << " impressions";
    EXPECT_GT(saves, 0u) << "no saves fired across " << events.size() << " impressions";
    EXPECT_GT(profileVisits, 0u);
}

// --- A stepV2-produced event round-trips through to_json with the V2 keys populated --------------
TEST(V2EventPipelineTest, StepV2EventRoundTripsThroughToJson) {
    const ExperimentConfig cfg = v2Config(/*latentReactions=*/true);
    const uint64_t seed = cfg.simulation.seed;
    GeneratedDataset ds = generateDataset(cfg.simulation, cfg.realism, seed);
    Simulator sim(cfg.behaviour, cfg.behaviourV2, cfg.reward, forkRng(seed, "behaviour"),
                  forkRng(seed, "satisfaction"), cfg.learning.recentWindow,
                  cfg.ranking.trendingHalfLifeSeconds);

    // Take an impression at feed position 3, from an exploration slot, so several V2 fields are at
    // clearly non-default values.
    Reel &reel = ds.reels[42];
    StepV2Inputs v2;
    v2.hiddenReel = &ds.hiddenReelStates[42];
    v2.positionInFeed = 3;
    v2.requestId = 777;
    v2.requestTimestamp = sim.now();
    v2.fromExploration = true;
    v2.sourceProvenance = CandidateSource::Exploration;
    LatentReaction latent;
    const StepResult sr = sim.stepV2(ds.users[0], ds.hiddenStates[0], reel,
                                     ds.creators[reel.creatorId.value], v2, latent);
    const InteractionEvent &e = sr.event;

    const nlohmann::json j = e; // to_json(InteractionEvent)
    // All thirteen V2 keys present and round-tripping.
    ASSERT_TRUE(j.contains("position_in_feed"));
    EXPECT_EQ(j.at("position_in_feed").get<uint32_t>(), e.positionInFeed);
    EXPECT_EQ(j.at("position_in_feed").get<uint32_t>(), 3u);
    EXPECT_EQ(j.at("request_id").get<uint64_t>(), 777u);
    EXPECT_EQ(j.at("request_timestamp").get<Timestamp>(), e.requestTimestamp);
    EXPECT_EQ(j.at("start_timestamp").get<Timestamp>(), e.startTimestamp);
    EXPECT_EQ(j.at("finish_timestamp").get<Timestamp>(), e.finishTimestamp);
    EXPECT_FLOAT_EQ(j.at("dwell_seconds").get<float>(), e.dwellSeconds);
    EXPECT_EQ(j.at("replay_count").get<uint32_t>(), e.replayCount);
    EXPECT_EQ(j.at("commented").get<bool>(), e.commented);
    EXPECT_EQ(j.at("saved").get<bool>(), e.saved);
    EXPECT_EQ(j.at("profile_visited").get<bool>(), e.profileVisited);
    EXPECT_TRUE(j.at("from_exploration").get<bool>());
    EXPECT_EQ(j.at("source_provenance").get<std::string>(), "exploration");
    EXPECT_FALSE(j.at("observed_exit_after_impression").get<bool>());
}

// --- Gate-off Simulator::step events carry all V2 fields at defaults (D17 shape) -----------------
TEST(V2EventPipelineTest, GateOffStepLeavesV2EventFieldsAtDefaults) {
    const ExperimentConfig cfg = v2Config(/*latentReactions=*/false);
    const uint64_t seed = cfg.simulation.seed;
    GeneratedDataset ds = generateDataset(cfg.simulation, cfg.realism, seed);

    // V1 constructor / V1 step -> no V2 fields written.
    Simulator sim(cfg.behaviour, cfg.reward, forkRng(seed, "behaviour"), cfg.learning.recentWindow,
                  cfg.ranking.trendingHalfLifeSeconds);
    for (int i = 0; i < 20; ++i) {
        Reel &reel = ds.reels[i];
        const StepResult sr =
            sim.step(ds.users[0], ds.hiddenStates[0], reel, ds.creators[reel.creatorId.value]);
        const InteractionEvent &e = sr.event;
        EXPECT_EQ(e.positionInFeed, 0u);
        EXPECT_EQ(e.requestId, 0u);
        EXPECT_EQ(e.requestTimestamp, 0);
        EXPECT_EQ(e.startTimestamp, 0);
        EXPECT_EQ(e.finishTimestamp, 0);
        EXPECT_EQ(e.dwellSeconds, 0.0F);
        EXPECT_EQ(e.replayCount, 0u);
        EXPECT_FALSE(e.commented);
        EXPECT_FALSE(e.saved);
        EXPECT_FALSE(e.profileVisited);
        EXPECT_FALSE(e.fromExploration);
        EXPECT_EQ(e.sourceProvenance, CandidateSource::VectorHNSW);
        EXPECT_FALSE(e.observedExitAfterImpression);
    }
}

// --- ExperimentRunner: welfare populated under the gate, absent without, and same-seed determinism
TEST(V2EventPipelineTest, RunnerWelfarePopulatedAndDeterministic) {
    const fs::path rootA = fs::path(::testing::TempDir()) / "rr_v2_pipeline_a";
    const fs::path rootB = fs::path(::testing::TempDir()) / "rr_v2_pipeline_b";
    fs::remove_all(rootA);
    fs::remove_all(rootB);

    ExperimentRunner runnerA(v2Config(/*latentReactions=*/true), rootA);
    ExperimentRunner runnerB(v2Config(/*latentReactions=*/true), rootB);
    const ExperimentResult a = runnerA.run();
    const ExperimentResult b = runnerB.run();

    // Welfare plumbing populated (evaluation carve-out): configured, every impression contributed a
    // latent reaction, and the per-round breakdown is present. (The MEAN values are 0 under A's
    // neutral stub — the magnitudes await A's real latent; this asserts the accumulation ran.)
    EXPECT_TRUE(a.welfare.configured);
    EXPECT_GT(a.welfare.impressions, 0u);
    EXPECT_EQ(a.welfare.impressions, a.impressionCount);
    EXPECT_EQ(a.welfare.byRound.size(), a.rounds.size());

    // Same seed -> identical deterministic fingerprint (welfare + engagement, no wall clock).
    EXPECT_EQ(fingerprint(a), fingerprint(b));

    // The gate-on summary.json carries a `welfare` block; a gate-off run carries none.
    const nlohmann::json summaryA = [&] {
        std::ifstream in(a.directory / "summary.json");
        return nlohmann::json::parse(in);
    }();
    EXPECT_TRUE(summaryA.contains("welfare"));
    EXPECT_TRUE(summaryA.at("welfare").contains("mean_immediate_satisfaction"));

    const fs::path rootOff = fs::path(::testing::TempDir()) / "rr_v2_pipeline_off";
    fs::remove_all(rootOff);
    ExperimentRunner runnerOff(v2Config(/*latentReactions=*/false), rootOff);
    const ExperimentResult off = runnerOff.run();
    EXPECT_FALSE(off.welfare.configured);
    EXPECT_EQ(off.welfare.impressions, 0u);
    const nlohmann::json summaryOff = [&] {
        std::ifstream in(off.directory / "summary.json");
        return nlohmann::json::parse(in);
    }();
    EXPECT_FALSE(summaryOff.contains("welfare"))
        << "gate-off summary.json must not carry a welfare block (D17 byte-identity)";
}
