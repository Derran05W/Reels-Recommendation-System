#include "rr/vindex/exact_vector_index.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "rr/core/embedding.hpp"

namespace rr {

namespace {

// Euclidean distance between two equal-length vectors, accumulated in double for precision.
// Identical inputs yield exactly 0, so a stored vector matches itself at distance 0.
float euclideanDistance(const Embedding &a, const Embedding &b) {
    double sumSq = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += diff * diff;
    }
    return static_cast<float>(std::sqrt(sumSq));
}

} // namespace

ExactVectorIndex::ExactVectorIndex(size_t dimensions) : dimensions_(dimensions) {}

void ExactVectorIndex::insert(const ReelId &id, const Embedding &embedding) {
    if (embedding.size() != dimensions_) {
        throw std::invalid_argument("ExactVectorIndex::insert: embedding dimension mismatch");
    }
    for (float v : embedding) {
        if (!std::isfinite(v)) {
            throw std::invalid_argument(
                "ExactVectorIndex::insert: embedding has a non-finite component");
        }
    }
    // emplace fails (and reports it) when the key already exists; check before mutating so the
    // stored set is untouched on a rejected duplicate.
    if (entries_.find(id) != entries_.end()) {
        throw std::invalid_argument("ExactVectorIndex::insert: duplicate reel id");
    }
    entries_.emplace(id, embedding);
}

std::vector<VectorSearchResult> ExactVectorIndex::search(const Embedding &query, size_t k) const {
    if (k == 0 || entries_.empty()) {
        return {};
    }
    if (query.size() != dimensions_) {
        throw std::invalid_argument("ExactVectorIndex::search: query dimension mismatch");
    }

    std::vector<VectorSearchResult> results;
    results.reserve(entries_.size());
    for (const auto &[id, embedding] : entries_) {
        const float distance = euclideanDistance(query, embedding);
        results.push_back(VectorSearchResult{id, distance, similarityFromEuclidean(distance)});
    }

    // Ascending distance; ties broken by ascending ReelId. ReelIds are unique, so this is a strict
    // total order and the sort is fully deterministic regardless of unordered_map iteration order.
    std::sort(results.begin(), results.end(),
              [](const VectorSearchResult &lhs, const VectorSearchResult &rhs) {
                  if (lhs.distance != rhs.distance) {
                      return lhs.distance < rhs.distance;
                  }
                  return lhs.reelId.value < rhs.reelId.value;
              });

    if (k < results.size()) {
        results.resize(k);
    }
    return results;
}

size_t ExactVectorIndex::size() const { return entries_.size(); }

} // namespace rr
