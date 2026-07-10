#include "rr/simulation/dataset_generator.hpp"

#include <utility>

#include "rr/infrastructure/random.hpp"
#include "rr/simulation/creator_generator.hpp"
#include "rr/simulation/reel_generator.hpp"
#include "rr/simulation/topic_generator.hpp"
#include "rr/simulation/user_generator.hpp"

namespace rr {

GeneratedDataset generateDataset(const SimulationConfig &config, uint64_t seed) {
    GeneratedDataset dataset;

    Rng topicsRng = forkRng(seed, "topics");
    dataset.topics = generateTopics(config, topicsRng);

    Rng creatorsRng = forkRng(seed, "creators");
    dataset.creators = generateCreators(config, dataset.topics, creatorsRng);

    Rng reelsRng = forkRng(seed, "reels");
    dataset.reels = generateReels(config, dataset.topics, dataset.creators, reelsRng);

    Rng usersRng = forkRng(seed, "users");
    GeneratedUsers generatedUsers = generateUsers(config, dataset.topics, usersRng);
    dataset.users = std::move(generatedUsers.users);
    dataset.hiddenStates = std::move(generatedUsers.hiddenStates);

    return dataset;
}

} // namespace rr
