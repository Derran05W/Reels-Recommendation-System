#include "rr/recommendation/weighted_ranker.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "rr/recommendation/feature_extractor.hpp"

namespace rr {

WeightedRanker::WeightedRanker(const std::vector<Reel> &reels, const RankingConfig &config,
                               bool contentV2)
    : config_(config), extractor_(reels, config, contentV2), contentV2_(contentV2) {}

std::vector<Candidate> WeightedRanker::rank(const User &user,
                                            const std::vector<Candidate> &candidates,
                                            Timestamp now) const {
    const std::vector<FeatureVector> features = extractor_.extract(user, candidates, now);

    std::vector<Candidate> ranked = candidates;
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        const FeatureVector &f = features[i];

        // TDD 14.2 weighted contributions. Penalties (repetition, impression fatigue) are stored
        // as NEGATIVE values so the map sums to the score directly. All ELEVEN FROZEN snake_case
        // keys are ALWAYS present. Computed in double, then stored as float; rankingScore is the
        // sum of the stored float contributions, so the map's values sum to rankingScore to within
        // one float rounding (property-tested).
        const float similarity = static_cast<float>(config_.similarityWeight * f.similarity);
        // session_topic (TDD 14.1): positive contribution, active from Phase 7 as session vectors
        // update online. session_topic_weight == 0 => zero contribution and Phase 6-identical
        // order.
        const float sessionTopic = static_cast<float>(config_.sessionTopicWeight * f.sessionTopic);
        const float quality = static_cast<float>(config_.qualityWeight * f.quality);
        const float freshness = static_cast<float>(config_.freshnessWeight * f.freshness);
        const float popularity = static_cast<float>(config_.popularityWeight * f.popularity);
        const float trending = static_cast<float>(config_.trendingWeight * f.trending);
        const float creatorAffinity =
            static_cast<float>(config_.creatorAffinityWeight * f.creatorAffinity);
        const float exploration = static_cast<float>(config_.explorationWeight * f.exploration);
        const float durationMatch =
            static_cast<float>(config_.durationMatchWeight * f.durationMatch);
        const float repetitionPenalty =
            static_cast<float>(-config_.repetitionPenalty * f.repetition);
        const float impressionPenalty =
            static_cast<float>(-config_.impressionPenaltyWeight * f.impressionCount);

        double sum = static_cast<double>(similarity) + sessionTopic + quality + freshness +
                     popularity + trending + creatorAffinity + exploration + durationMatch +
                     repetitionPenalty + impressionPenalty;

        Candidate &c = ranked[i];
        c.featureContributions = {
            {"similarity", similarity},
            {"session_topic", sessionTopic},
            {"quality", quality},
            {"freshness", freshness},
            {"popularity", popularity},
            {"trending", trending},
            {"creator_affinity", creatorAffinity},
            {"exploration", exploration},
            {"duration_match", durationMatch},
            {"repetition_penalty", repetitionPenalty},
            {"impression_penalty", impressionPenalty},
        };

        // Realism V2 gated contributions (Phase 15, plan task 3): weight * feature for each of the
        // ten V2 features, emitted ONLY under contentV2_. Every V2 term is a POSITIVE weighted term
        // (weight * feature); the SIGN comes from the config weight, so a NEGATIVE preset weight
        // (e.g. the satisfaction-proxy arm penalizing clickbait) flows through as a negative
        // contribution. Added to BOTH the running score and the map so it keeps summing to
        // rankingScore. Gate-off: none added, so the score AND the eleven-key map are
        // byte-identical to the pre-Phase-15 ranker (D17).
        if (contentV2_) {
            const float visualMatch = static_cast<float>(config_.visualMatchWeight * f.visualMatch);
            const float musicMatch = static_cast<float>(config_.musicMatchWeight * f.musicMatch);
            const float emotionalMatch =
                static_cast<float>(config_.emotionalMatchWeight * f.emotionalMatch);
            const float clickbait = static_cast<float>(config_.clickbaitWeight * f.clickbait);
            const float emotionalIntensity =
                static_cast<float>(config_.emotionalIntensityWeight * f.emotionalIntensity);
            const float usefulness = static_cast<float>(config_.usefulnessWeight * f.usefulness);
            const float productionQuality =
                static_cast<float>(config_.productionQualityWeight * f.productionQuality);
            const float informationDensity =
                static_cast<float>(config_.informationDensityWeight * f.informationDensity);
            const float languageMatch =
                static_cast<float>(config_.languageMatchWeight * f.languageMatch);
            const float savePopularity =
                static_cast<float>(config_.savePopularityWeight * f.savePopularity);

            sum += static_cast<double>(visualMatch) + musicMatch + emotionalMatch + clickbait +
                   emotionalIntensity + usefulness + productionQuality + informationDensity +
                   languageMatch + savePopularity;

            c.featureContributions["visual_match"] = visualMatch;
            c.featureContributions["music_match"] = musicMatch;
            c.featureContributions["emotional_match"] = emotionalMatch;
            c.featureContributions["clickbait"] = clickbait;
            c.featureContributions["emotional_intensity"] = emotionalIntensity;
            c.featureContributions["usefulness"] = usefulness;
            c.featureContributions["production_quality"] = productionQuality;
            c.featureContributions["information_density"] = informationDensity;
            c.featureContributions["language_match"] = languageMatch;
            c.featureContributions["save_popularity"] = savePopularity;
        }

        c.rankingScore = static_cast<float>(sum);
    }

    // Sort by rankingScore DESCENDING, ties by ascending ReelId. ReelIds are unique in a
    // deduplicated pool, so this is a TOTAL order and the output is fully deterministic.
    std::sort(ranked.begin(), ranked.end(), [](const Candidate &a, const Candidate &b) {
        if (a.rankingScore != b.rankingScore) {
            return a.rankingScore > b.rankingScore;
        }
        return a.reelId.value < b.reelId.value;
    });

    return ranked;
}

} // namespace rr
