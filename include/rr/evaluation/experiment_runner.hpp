#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rr/evaluation/metrics_collector.hpp"
#include "rr/evaluation/run_metadata.hpp"
#include "rr/infrastructure/config.hpp"

namespace rr {

// Per-round row (one request per user per round). Bundles the §18.3 behaviour metrics for the
// round with the round's oracle-regret aggregates.
struct RoundMetrics {
    size_t round = 0;
    MetricsSummary metrics;
    size_t sampledRequests = 0; // requests whose Bernoulli(oracleSampleRate) draw fired this round
    double meanRegret = 0.0;    // mean regret over this round's sampled requests (0 if none)
    double cumulativeRegret = 0.0; // running sum of sampled-request regret through this round
};

// Everything one experiment produced, in memory. The ResultsWriter serializes it to disk; the
// simulate CLI prints headline lines from it. `directory` is the created <experiment-id> dir.
struct ExperimentResult {
    ExperimentConfig config;
    uint64_t seed = 0;
    std::string experimentId;
    std::filesystem::path directory;

    size_t userCount = 0;
    size_t reelCount = 0;
    size_t requestCount = 0;
    size_t impressionCount = 0;

    MetricsSummary overall;

    double oracleSampleRate = 0.0;
    size_t sampledRequestCount = 0;
    double meanRegret = 0.0;
    double cumulativeRegret = 0.0;

    std::vector<RoundMetrics> rounds;

    // Wall-clock, confined to summary.timing + latency_metrics.csv (D9/D8 determinism carve-out).
    LatencyStats latency;
    double totalWallSeconds = 0.0;
};

// Runs the end-to-end baseline evaluation loop (TDD 20 + phase-4 task 4) from a fully-resolved
// config and writes the §26 output layout under <outputRoot>/<experiment-id>/.
//
// Flow: generateDataset -> cold-start (static estimates, no online learning until Phase 7) ->
// interleaved requestsPerUser rounds over all users -> per-impression behaviour metrics + §18.2
// true affinity -> oracle regret on a Bernoulli-sampled subset -> results on disk.
//
// The master seed is config.simulation.seed; independent named rng streams (behaviour /
// recommender / oracle, D8) keep the oracle from perturbing the simulation.
class ExperimentRunner {
  public:
    ExperimentRunner(ExperimentConfig config, std::filesystem::path outputRoot,
                     BuildProvenance provenance = {});

    // Generate, simulate, aggregate, and write all output files. Returns the in-memory result.
    ExperimentResult run();

  private:
    ExperimentConfig config_;
    std::filesystem::path outputRoot_;
    BuildProvenance provenance_;
};

} // namespace rr
