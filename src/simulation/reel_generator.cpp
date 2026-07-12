#include "rr/simulation/reel_generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "rr/core/embedding.hpp"

namespace rr {
namespace {

// Embedding construction weights (TDD 9.2): v = normalize(w1*t1 + w2*t2 + ws*style + noise). The
// primary topic dominates; the secondary and creator-style terms tilt the vector without
// displacing the primary, so reel.primaryTopic reliably reflects the strongest contributor.
constexpr float kPrimaryWeight = 1.00f;
constexpr float kSecondaryWeight = 0.50f;
constexpr float kStyleWeight = 0.30f;

// Noise: kNoiseMagnitude is the approximate total L2 magnitude of the noise vector, made
// dimension-independent by dividing the per-component stddev by sqrt(dim). Kept below the unit
// topic signal so the topic structure survives.
constexpr double kNoiseMagnitude = 0.30;

// Probability the primary topic is drawn from the creator's specialties (vs. a uniform topic).
// This is what links reels to creators via topic overlap, enabling creator-affinity experiments
// (TDD 9.4). kSecondaryProb is the probability a reel carries a secondary topic at all.
constexpr double kSpecialtyBias = 0.80;
constexpr double kSecondaryProb = 0.50;

// intrinsicQuality ~ N(creator.baseQuality, kQualityStddev) clamped to [0, 1]: reels track their
// creator's quality with a small per-reel jitter.
constexpr double kQualityStddev = 0.10;

// Duration buckets in seconds (TDD 9.2): equal-probability bucket choice, then a uniform draw
// within the chosen bucket. The union covers [5, 120) seconds.
struct DurationBucket {
    double lo;
    double hi;
};
constexpr DurationBucket kDurationBuckets[] = {
    {5.0, 15.0}, {15.0, 30.0}, {30.0, 60.0}, {60.0, 120.0}};
constexpr uint64_t kNumDurationBuckets = 4;

// createdAt is spread uniformly over a fixed 30-day window of logical simulated seconds (D9). A
// fixed constant keeps the distribution nobody has asked to tune off the config surface.
constexpr uint64_t kCreationWindowSeconds = 30ULL * 24 * 60 * 60; // 2,592,000

float clamp01(double v) { return static_cast<float>(std::clamp(v, 0.0, 1.0)); }

} // namespace

std::vector<Reel> generateReels(const SimulationConfig &config, const std::vector<Topic> &topics,
                                const std::vector<Creator> &creators, Rng &rng, uint32_t idOffset) {
    std::vector<Reel> reels;
    if (topics.empty() || creators.empty()) {
        return reels; // No topic or creator to attach a reel to; documented no-crash fallback.
    }
    reels.reserve(config.reels);

    const uint32_t dim = config.dimensions;
    const double noisePerDim = kNoiseMagnitude / std::sqrt(static_cast<double>(dim));

    for (uint32_t i = 0; i < config.reels; ++i) {
        Reel r{}; // zero-initializes counters, scores, flags.
        r.id = ReelId{idOffset + i};

        // Creator: uniform over all creators.
        const Creator &creator = creators[rng.uniformInt(creators.size())];
        r.creatorId = creator.id;

        // Primary topic: biased toward the creator's specialties, otherwise a uniform topic.
        TopicId t1;
        if (!creator.topicSpecialties.empty() && rng.bernoulli(kSpecialtyBias)) {
            t1 = creator.topicSpecialties[rng.uniformInt(creator.topicSpecialties.size())];
        } else {
            t1 = topics[rng.uniformInt(topics.size())].id;
        }

        // Optional secondary topic, distinct from the primary.
        const bool hasSecondary = topics.size() > 1u && rng.bernoulli(kSecondaryProb);
        TopicId t2{};
        if (hasSecondary) {
            do {
                t2 = topics[rng.uniformInt(topics.size())].id;
            } while (t2 == t1);
        }

        // Embedding = normalize(w1*t1 + [w2*t2] + ws*style + noise).
        Embedding e(dim, 0.0f);
        const Embedding &c1 = topics[t1.value].centre;
        for (uint32_t d = 0; d < dim; ++d) {
            e[d] += kPrimaryWeight * c1[d];
        }
        if (hasSecondary) {
            const Embedding &c2 = topics[t2.value].centre;
            for (uint32_t d = 0; d < dim; ++d) {
                e[d] += kSecondaryWeight * c2[d];
            }
        }
        for (uint32_t d = 0; d < dim; ++d) {
            e[d] += kStyleWeight * creator.styleEmbedding[d];
        }
        for (uint32_t d = 0; d < dim; ++d) {
            e[d] += static_cast<float>(noisePerDim * rng.gaussian());
        }
        normalize(e);
        r.embedding = std::move(e);

        // Topic labels: exactly the ids used to build the embedding (directly-tested invariant).
        r.primaryTopic = t1;
        if (hasSecondary) {
            r.secondaryTopics = {t2};
        }

        // Duration: pick a bucket uniformly, draw uniformly within it.
        const DurationBucket &bucket = kDurationBuckets[rng.uniformInt(kNumDurationBuckets)];
        r.durationSeconds = static_cast<float>(rng.uniform(bucket.lo, bucket.hi));

        // Quality: clamped gaussian around the creator's base quality.
        r.intrinsicQuality = clamp01(creator.baseQuality + kQualityStddev * rng.gaussian());

        // freshnessScore is derived later by the freshness source (Phase 4); zero at generation.
        r.freshnessScore = 0.0f;

        // createdAt: uniform over the creation window (logical seconds).
        r.createdAt = rng.uniformInt(kCreationWindowSeconds);

        // Engagement counters start zeroed (plan Phase 2 task 3).
        r.impressionCount = 0;
        r.completionCount = 0;
        r.likeCount = 0;
        r.shareCount = 0;
        r.skipCount = 0;
        r.active = true;

        reels.push_back(std::move(r));
    }
    return reels;
}

} // namespace rr
