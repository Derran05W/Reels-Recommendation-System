// inspect_user — dumps topic/reel/creator/user distribution summaries as JSON (Phase 2, task 7).
//
// A first-cut eyeballing tool: generates a full synthetic dataset via rr::generateDataset and
// reports a nearest-topic histogram (for reels and for users, by nearest topic centre) and a
// quality histogram (reel intrinsicQuality, creator baseQuality), plus basic scale counts. This is
// an inspection tool, not a benchmark (no timing claims) — see apps/benchmark_retrieval.cpp for
// that.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rr/core/embedding.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/simulation/dataset_generator.hpp"

namespace {

// Index of the topic whose centre is closest (max dot product; embeddings are unit vectors so
// this ranks identically to nearest-by-Euclidean-distance, design decision D3) to `e`.
size_t nearestTopic(const rr::Embedding &e, const std::vector<rr::Topic> &topics) {
    size_t best = 0;
    float bestDot = -2.0f;
    for (size_t i = 0; i < topics.size(); ++i) {
        const float d = rr::dot(e, topics[i].centre);
        if (d > bestDot) {
            bestDot = d;
            best = i;
        }
    }
    return best;
}

std::vector<uint64_t> histogram(const std::vector<float> &values, int numBins) {
    std::vector<uint64_t> bins(static_cast<size_t>(numBins), 0);
    for (float v : values) {
        int bin = std::clamp(static_cast<int>(v * static_cast<float>(numBins)), 0, numBins - 1);
        ++bins[static_cast<size_t>(bin)];
    }
    return bins;
}

} // namespace

int main(int argc, char **argv) {
    std::string configPath;
    uint64_t seed = 42;
    std::string outPath; // empty => stdout

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << what << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--config") {
            configPath = next("--config");
        } else if (a == "--seed") {
            seed = std::stoull(next("--seed"));
        } else if (a == "--out") {
            outPath = next("--out");
        } else if (a == "--help" || a == "-h") {
            std::cout << "usage: inspect_user [--config path] [--seed N] [--out path]\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            return 2;
        }
    }

    rr::SimulationConfig config; // defaults: 10k users / 100k reels / 5k creators / 32 topics.
    if (!configPath.empty()) {
        config = rr::loadExperimentConfig(configPath).simulation;
    }

    rr::GeneratedDataset dataset = rr::generateDataset(config, seed);

    std::vector<uint64_t> reelTopicHistogram(dataset.topics.size(), 0);
    std::vector<float> reelQualities;
    reelQualities.reserve(dataset.reels.size());
    for (const auto &r : dataset.reels) {
        ++reelTopicHistogram[nearestTopic(r.embedding, dataset.topics)];
        reelQualities.push_back(r.intrinsicQuality);
    }

    std::vector<uint64_t> userTopicHistogram(dataset.topics.size(), 0);
    for (const auto &h : dataset.hiddenStates) {
        ++userTopicHistogram[nearestTopic(h.hiddenPreference, dataset.topics)];
    }

    std::vector<float> creatorQualities;
    creatorQualities.reserve(dataset.creators.size());
    for (const auto &c : dataset.creators) {
        creatorQualities.push_back(c.baseQuality);
    }

    nlohmann::json out;
    out["seed"] = seed;
    out["counts"] = {{"topics", dataset.topics.size()},
                      {"creators", dataset.creators.size()},
                      {"reels", dataset.reels.size()},
                      {"users", dataset.users.size()}};
    out["reel_nearest_topic_histogram"] = reelTopicHistogram;
    out["user_nearest_topic_histogram"] = userTopicHistogram;
    out["reel_quality_histogram"] = histogram(reelQualities, 10);
    out["creator_quality_histogram"] = histogram(creatorQualities, 10);

    if (outPath.empty()) {
        std::cout << out.dump(2) << "\n";
    } else {
        std::filesystem::path p(outPath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        std::ofstream f(p);
        f << out.dump(2) << "\n";
        std::cerr << "wrote " << p.string() << "\n";
    }

    return 0;
}
