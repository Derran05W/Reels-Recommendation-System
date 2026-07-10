#include "rr/simulation/topic_generator.hpp"

#include <utility>

#include "rr/core/embedding.hpp"

namespace rr {
namespace {

// A random point on the unit sphere: each component is an independent standard normal, then the
// vector is L2-normalized. Isotropic, so topic centres are spread evenly over the sphere.
Embedding randomUnitEmbedding(uint32_t dim, Rng &rng) {
    Embedding e(dim);
    for (uint32_t d = 0; d < dim; ++d) {
        e[d] = static_cast<float>(rng.gaussian());
    }
    normalize(e);
    return e;
}

} // namespace

std::vector<Topic> generateTopics(const SimulationConfig &config, Rng &rng) {
    std::vector<Topic> topics;
    topics.reserve(config.topics);
    for (uint32_t i = 0; i < config.topics; ++i) {
        Topic t;
        t.id = TopicId{i};
        t.centre = randomUnitEmbedding(config.dimensions, rng);
        topics.push_back(std::move(t));
    }
    return topics;
}

} // namespace rr
