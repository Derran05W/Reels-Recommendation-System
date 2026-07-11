#include "rr/evaluation/oracle.hpp"

#include <algorithm>
#include <cstddef>

#include "rr/core/embedding.hpp"

namespace rr {

float trueAffinity(const Embedding &hiddenPreference, const Reel &reel) {
    return dot(hiddenPreference, reel.embedding);
}

OracleResult computeOracleRegret(const Embedding &hiddenPreference, const std::vector<Reel> &reels,
                                 const std::unordered_set<ReelId> &seenBeforeFeed,
                                 const std::vector<ReelId> &recommendedFeed, size_t feedSize) {
    OracleResult result;

    // Score every active reel the user has not yet seen (state BEFORE the feed is consumed).
    struct Scored {
        float affinity;
        uint32_t id;
    };
    std::vector<Scored> candidates;
    candidates.reserve(reels.size());
    for (const Reel &reel : reels) {
        if (!reel.active || seenBeforeFeed.contains(reel.id)) {
            continue;
        }
        candidates.push_back({trueAffinity(hiddenPreference, reel), reel.id.value});
    }

    // Sort by affinity descending, breaking ties by ascending id so the top-k is deterministic
    // (D8) even when affinities coincide.
    std::sort(candidates.begin(), candidates.end(), [](const Scored &a, const Scored &b) {
        if (a.affinity != b.affinity) {
            return a.affinity > b.affinity;
        }
        return a.id < b.id;
    });

    const size_t oracleK = std::min(feedSize, candidates.size());
    double oracleSum = 0.0;
    for (size_t i = 0; i < oracleK; ++i) {
        oracleSum += static_cast<double>(candidates[i].affinity);
    }
    result.oracleK = oracleK;
    result.oracleMeanAffinity = oracleK > 0 ? oracleSum / static_cast<double>(oracleK) : 0.0;

    // Mean true affinity of the reels the recommender actually returned.
    double recSum = 0.0;
    size_t recCount = 0;
    for (const ReelId &id : recommendedFeed) {
        if (id.value >= reels.size()) {
            continue; // defensive: ids index the dense reel table
        }
        recSum += static_cast<double>(trueAffinity(hiddenPreference, reels[id.value]));
        ++recCount;
    }
    result.recommendedK = recCount;
    result.recommendedMeanAffinity = recCount > 0 ? recSum / static_cast<double>(recCount) : 0.0;

    result.regret = result.oracleMeanAffinity - result.recommendedMeanAffinity;
    return result;
}

} // namespace rr
