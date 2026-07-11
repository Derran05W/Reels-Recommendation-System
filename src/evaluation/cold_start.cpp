#include "rr/evaluation/cold_start.hpp"

#include <stdexcept>

namespace rr {

Embedding globalAveragePreference(const std::vector<HiddenUserState> &hidden) {
    if (hidden.empty()) {
        throw std::invalid_argument("globalAveragePreference: no users");
    }
    const size_t dims = hidden.front().hiddenPreference.size();
    std::vector<double> sum(dims, 0.0);
    for (const HiddenUserState &h : hidden) {
        if (h.hiddenPreference.size() != dims) {
            throw std::invalid_argument("globalAveragePreference: inconsistent dimensions");
        }
        for (size_t d = 0; d < dims; ++d) {
            sum[d] += h.hiddenPreference[d];
        }
    }
    Embedding mean(dims);
    for (size_t d = 0; d < dims; ++d) {
        mean[d] = static_cast<float>(sum[d] / static_cast<double>(hidden.size()));
    }
    normalize(mean); // throws if the population mean has no direction
    return mean;
}

void applyColdStart(std::vector<User> &users, const Embedding &prior) {
    for (User &user : users) {
        user.estimatedPreference = prior;
        user.longTermPreference = prior;
        user.sessionPreference = prior;
    }
}

} // namespace rr
