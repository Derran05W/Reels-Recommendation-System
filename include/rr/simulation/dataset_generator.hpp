#pragma once

#include <cstdint>
#include <vector>

#include "rr/domain/creator.hpp"
#include "rr/domain/hidden_user_state.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"

namespace rr {

// A full synthetic dataset (TDD §9): topics, creators, reels, and users with their ground-truth
// hidden state, generated together from one master seed.
struct GeneratedDataset {
    std::vector<Topic> topics;
    std::vector<Creator> creators;
    std::vector<Reel> reels;
    std::vector<User> users;
    std::vector<HiddenUserState> hiddenStates;
};

// Generates topics, then creators, then reels, then users, each on its own named Rng stream
// forked from `seed` ("topics" / "creators" / "reels" / "users", design decision D8). Because the
// streams are independent, regenerating one subsystem (e.g. widening config.reels) never changes
// another (e.g. the generated users) — same seed, same stream name, same output.
GeneratedDataset generateDataset(const SimulationConfig &config, uint64_t seed);

} // namespace rr
