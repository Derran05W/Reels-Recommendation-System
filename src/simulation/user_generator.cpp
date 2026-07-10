#include "rr/simulation/user_generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

#include "rr/core/embedding.hpp"

namespace rr {

GeneratedUsers generateUsers(const SimulationConfig &config, const std::vector<Topic> &topics,
                             Rng &rng) {
    if (config.users > 0 && topics.empty()) {
        throw std::invalid_argument(
            "rr::generateUsers: topics must be non-empty to build user preferences");
    }

    GeneratedUsers out;
    out.users.reserve(config.users);
    out.hiddenStates.reserve(config.users);

    const uint32_t dim = config.dimensions;
    const uint64_t topicCount = topics.size();

    // Reusable index buffer for distinct-topic selection via partial Fisher-Yates. It stays a
    // permutation of [0, topicCount) across users, so distinctness holds regardless of prior order.
    std::vector<uint32_t> topicIdx(static_cast<size_t>(topicCount));
    std::iota(topicIdx.begin(), topicIdx.end(), 0u);

    for (uint32_t u = 0; u < config.users; ++u) {
        HiddenUserState hidden;
        hidden.userId = UserId{u};

        // Preferred-topic count in [kMin, kMax], clamped to the number of available topics.
        const uint32_t span = static_cast<uint32_t>(userTraits::kMaxPreferredTopics -
                                                     userTraits::kMinPreferredTopics);
        const uint32_t desired =
            static_cast<uint32_t>(userTraits::kMinPreferredTopics) +
            static_cast<uint32_t>(rng.uniformInt(span + 1));
        const uint32_t m = static_cast<uint32_t>(std::min<uint64_t>(desired, topicCount));

        // Preference concentration: a single draw that both shapes the weight peakedness and is
        // stored as the user's trait.
        hidden.preferenceConcentration = static_cast<float>(
            rng.uniform(userTraits::kConcentrationLo, userTraits::kConcentrationHi));

        // Select m distinct topics via partial Fisher-Yates over topicIdx.
        for (uint32_t k = 0; k < m; ++k) {
            const uint64_t j = k + rng.uniformInt(topicCount - k);
            std::swap(topicIdx[k], topicIdx[static_cast<size_t>(j)]);
        }

        // Weighted topic mix: a_i = base^concentration with base in [kWeightBaseLo, 1). Higher
        // concentration widens the spread between the largest and smallest weight => more peaked.
        Embedding pref(dim, 0.0f);
        hidden.preferredTopics.reserve(m);
        for (uint32_t k = 0; k < m; ++k) {
            const uint32_t ti = topicIdx[k];
            hidden.preferredTopics.push_back(topics[ti].id);
            const double base = rng.uniform(userTraits::kWeightBaseLo, 1.0);
            const float a = static_cast<float>(
                std::pow(base, static_cast<double>(hidden.preferenceConcentration)));
            const Embedding &centre = topics[ti].centre;
            for (uint32_t d = 0; d < dim; ++d) {
                pref[d] += a * centre[d];
            }
        }

        // Additive gaussian noise ε, then L2-normalize into the latent preference.
        for (uint32_t d = 0; d < dim; ++d) {
            pref[d] += static_cast<float>(rng.gaussian() * userTraits::kNoiseScale);
        }
        normalize(pref);
        hidden.hiddenPreference = std::move(pref);

        // Remaining behavioural traits, in a fixed draw order (determinism depends only on order).
        hidden.exploreWillingness =
            static_cast<float>(rng.uniform(userTraits::kExploreLo, userTraits::kExploreHi));
        hidden.avgSessionLength = static_cast<float>(
            rng.uniform(userTraits::kSessionLengthLo, userTraits::kSessionLengthHi));
        hidden.likePropensity = static_cast<float>(
            rng.uniform(userTraits::kLikePropensityLo, userTraits::kLikePropensityHi));
        hidden.sharePropensity = static_cast<float>(
            rng.uniform(userTraits::kSharePropensityLo, userTraits::kSharePropensityHi));
        hidden.durationTolerance = static_cast<float>(
            rng.uniform(userTraits::kDurationToleranceLo, userTraits::kDurationToleranceHi));
        hidden.preferenceStability =
            static_cast<float>(rng.uniform(userTraits::kStabilityLo, userTraits::kStabilityHi));

        // Public, recommender-visible User: no hidden state (D11). Estimated / long-term / session
        // preferences are left empty here; cold-start initialization is Phase 4's job.
        User user{};
        user.id = UserId{u};
        user.totalInteractions = 0;
        user.currentSessionLength = 0;

        out.users.push_back(std::move(user));
        out.hiddenStates.push_back(std::move(hidden));
    }

    return out;
}

} // namespace rr
