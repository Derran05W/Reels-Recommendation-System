#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/recommendation/vector_index.hpp"

namespace rr {

// Wraps vector-db's HNSWIndex directly (design decision D2 — never the VectorDatabase facade,
// which cannot configure efConstruction/efSearch independently). The vector-db type is held
// behind a pointer whose definition lives only in the .cpp under src/vindex/, so this header
// stays free of vector-db symbols (D2 containment: only src/vindex/*.cpp may include vector-db
// headers).
class HNSWVectorIndex : public VectorIndex {
  public:
    HNSWVectorIndex(size_t dimensions, const HNSWConfig &config, uint64_t seed);
    ~HNSWVectorIndex() override;

    HNSWVectorIndex(const HNSWVectorIndex &) = delete;
    HNSWVectorIndex &operator=(const HNSWVectorIndex &) = delete;

    // Throws std::invalid_argument on duplicate id, dimension mismatch, or a non-finite
    // embedding. Inputs are validated before the vector-db insert call so a vector-db-side throw
    // never occurs in normal operation (D2).
    void insert(const ReelId &id, const Embedding &embedding) override;

    // Ascending-distance top-k via vector-db HNSW search; distances are converted to similarity
    // with rr::similarityFromEuclidean (D3). k == 0 or empty index => {}.
    std::vector<VectorSearchResult> search(const Embedding &query, size_t k) const override;

    size_t size() const override;

    // Passthrough to vector-db's setEfSearch, so a benchmark can sweep efSearch without
    // rebuilding the graph.
    void setEfSearch(size_t ef);

    // Passthrough to vector-db's getLevelDistribution (needed by apps/benchmark_retrieval.cpp).
    std::vector<size_t> getLevelDistribution() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rr
