#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"
#include "rr/recommendation/vector_index.hpp"

namespace rr {

// Brute-force flat scan over stored embeddings (TDD 12.2 / design decision: exact by
// construction, used as ground truth / recall ceiling / ANN comparison baseline — never as a
// large-scale online index).
class ExactVectorIndex : public VectorIndex {
  public:
    explicit ExactVectorIndex(size_t dimensions);

    // Throws std::invalid_argument on duplicate id, dimension mismatch, or a non-finite
    // embedding (mirrors HNSWVectorIndex's validation contract so the two indexes are
    // differential-test comparable on the same inputs).
    void insert(const ReelId &id, const Embedding &embedding) override;

    // Ascending-Euclidean-distance top-k, ties broken by ascending ReelId for determinism.
    // k == 0 or empty index => {}. k > size() => all elements, sorted.
    std::vector<VectorSearchResult> search(const Embedding &query, size_t k) const override;

    size_t size() const override;

  private:
    size_t dimensions_;
    std::unordered_map<ReelId, Embedding> entries_;
};

} // namespace rr
