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

// Graph-shape snapshot for the TDD 17.3 benchmarks ("Graph degree distribution" / "Maximum graph
// level"). D2 containment: this mirrors vector-db's HNSWIndex::IndexStats using ONLY std types, so
// the vector-db symbol never leaks into this header.
//   levelDistribution[l]       = count of nodes whose TOP level == l (sums to nodeCount).
//   degreeHistogramLevel0[d]   = count of nodes with exactly d neighbours at level 0 (sums to
//                                nodeCount; d never exceeds M0 == 2*M).
struct HnswGraphStats {
    std::size_t nodeCount = 0;
    int maxLevel = -1; // top level of the graph; -1 when empty.
    std::vector<std::size_t> levelDistribution;
    std::vector<std::size_t> degreeHistogramLevel0;
};

// Wraps vector-db's HNSWIndex directly (design decision D2 — never the VectorDatabase facade,
// which cannot configure efConstruction/efSearch independently). The vector-db type is held
// behind a pointer whose definition lives only in the .cpp under src/vindex/, so this header
// stays free of vector-db symbols (D2 containment: only src/vindex/*.cpp may include vector-db
// headers).
class HNSWVectorIndex : public VectorIndex {
  public:
    // countDistanceComps opts into distance-computation counting (TDD 17.3 "Distance computations
    // per query, if feasible"): a counting decorator wraps the SAME Euclidean metric the index
    // uses by default, so distance VALUES and search results stay bit-identical whether counting is
    // on or off — only the reported counter differs. Default false keeps the Phase 1 hot path
    // instrumentation-free. See distanceComputations() for the latency caveat.
    HNSWVectorIndex(size_t dimensions, const HNSWConfig &config, uint64_t seed,
                    bool countDistanceComps = false);
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

    // Full graph-shape snapshot (level distribution + level-0 degree histogram + max level).
    // Independent of whether distance counting is enabled — the graph is identical either way.
    HnswGraphStats graphStats() const;

    // Total distance() invocations since construction (or the last resetDistanceCounter()). Counts
    // BOTH insertion-time and search-time distances, so measure "per query" by calling
    // resetDistanceCounter() after the build and dividing by the query count. Returns 0 when
    // counting was not enabled at construction. NOTE: a counting-enabled index pays a
    // relaxed-atomic increment per distance, so its measured LATENCY is not clean (take latency
    // from a counting-off index/pass).
    uint64_t distanceComputations() const;

    // Zero the distance counter (no-op when counting is disabled). Lets a caller separate the
    // build phase from a per-query measurement phase.
    void resetDistanceCounter();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rr
