#include "rr/simulation/creator_generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "rr/core/embedding.hpp"

namespace rr {
namespace {

// baseQuality ~ N(kQualityMean, kQualityStddev) clamped to [0, 1]. A moderate spread keeps most
// creators mid-quality while allowing a tail of strong/weak creators.
constexpr double kQualityMean = 0.5;
constexpr double kQualityStddev = 0.15;

// Style noise. kStyleNoiseMagnitude is the (approximate) total L2 magnitude of the noise vector;
// dividing the per-component stddev by sqrt(dim) makes that magnitude dimension-independent, and
// keeping it well below the unit-magnitude topic signal ensures specialties dominate the style.
constexpr double kStyleNoiseMagnitude = 0.30;

// A creator specializes in 1-3 distinct topics (TDD 9.4).
constexpr uint32_t kMinSpecialties = 1;
constexpr uint32_t kMaxSpecialties = 3;

float clamp01(double v) { return static_cast<float>(std::clamp(v, 0.0, 1.0)); }

} // namespace

std::vector<Creator> generateCreators(const SimulationConfig &config,
                                      const std::vector<Topic> &topics, Rng &rng) {
    std::vector<Creator> creators;
    creators.reserve(config.creators);

    const uint32_t dim = config.dimensions;
    const double noisePerDim = kStyleNoiseMagnitude / std::sqrt(static_cast<double>(dim));

    for (uint32_t i = 0; i < config.creators; ++i) {
        Creator c;
        c.id = CreatorId{i};

        // Sample 1-3 distinct specialties (capped by the number of topics available). With an
        // empty topic list this yields no specialties and a noise-only style embedding.
        const uint64_t span = kMaxSpecialties - kMinSpecialties + 1;
        uint32_t want = kMinSpecialties + static_cast<uint32_t>(rng.uniformInt(span));
        if (want > topics.size()) {
            want = static_cast<uint32_t>(topics.size());
        }
        std::vector<TopicId> specialties;
        specialties.reserve(want);
        while (specialties.size() < want) {
            TopicId cand = topics[rng.uniformInt(topics.size())].id;
            if (std::find(specialties.begin(), specialties.end(), cand) == specialties.end()) {
                specialties.push_back(cand);
            }
        }

        // styleEmbedding = normalize(sum of specialty centres + noise).
        Embedding style(dim, 0.0f);
        for (TopicId t : specialties) {
            const Embedding &centre = topics[t.value].centre;
            for (uint32_t d = 0; d < dim; ++d) {
                style[d] += centre[d];
            }
        }
        for (uint32_t d = 0; d < dim; ++d) {
            style[d] += static_cast<float>(noisePerDim * rng.gaussian());
        }
        normalize(style);

        c.styleEmbedding = std::move(style);
        c.topicSpecialties = std::move(specialties);
        c.baseQuality = clamp01(kQualityMean + kQualityStddev * rng.gaussian());

        creators.push_back(std::move(c));
    }
    return creators;
}

} // namespace rr
