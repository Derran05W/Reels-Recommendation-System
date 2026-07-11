#pragma once

#include <cstddef>
#include <vector>

#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"

namespace rr {

// Shared feed-eligibility predicate for every baseline recommender (TDD 13 items 4 & 5): a reel
// may enter a feed iff it is active AND the user has not already seen it. Feeds never repeat items
// and never surface deactivated reels. This is the single place that rule lives so all three
// baselines agree.
inline bool isEligible(const Reel &reel, const User &user) {
    return reel.active && !user.seenReels.contains(reel.id);
}

// Indices (into `reels`) of every reel eligible for `user`, in ascending index order. Under the
// dense-id invariant (reels[i].id.value == i) that is also ascending ReelId order, giving every
// recommender a deterministic base ordering to sample / score / tie-break over.
inline std::vector<std::size_t> eligibleReelIndices(const std::vector<Reel> &reels,
                                                    const User &user) {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < reels.size(); ++i) {
        if (isEligible(reels[i], user)) {
            indices.push_back(i);
        }
    }
    return indices;
}

} // namespace rr
