#include "rr/vindex/hnsw_vector_index.hpp"

// D2 containment: this is one of the few translation units permitted to include vector-db headers.
// vector-db symbols live in the GLOBAL namespace (::HNSWIndex, ::HNSWConfig, ::Vector), so they are
// always qualified with :: here to keep them distinct from rr::HNSWConfig et al.
#include "hnsw_index.hpp"
#include "vector.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace rr {

// Holds the concrete vector-db index. Defined only in this .cpp so the public header stays free of
// vector-db symbols (pimpl, per the frozen contract).
struct HNSWVectorIndex::Impl {
    ::HNSWIndex index;
    std::size_t dimensions;

    Impl(std::size_t dims, const HNSWConfig &config, std::uint64_t seed)
        // Translate rr::HNSWConfig field names to vector-db's (m->M, efConstruction->ef_construction,
        // efSearch->ef_search) and thread the seed through (D8). metric defaults to nullptr, so
        // vector-db constructs its own EuclideanDistance, matching D3.
        : index(dims,
                ::HNSWConfig{static_cast<std::size_t>(config.m),
                             static_cast<std::size_t>(config.efConstruction),
                             static_cast<std::size_t>(config.efSearch), seed}),
          dimensions(dims) {}
};

HNSWVectorIndex::HNSWVectorIndex(std::size_t dimensions, const HNSWConfig &config,
                                 std::uint64_t seed)
    : impl_(std::make_unique<Impl>(dimensions, config, seed)) {}

HNSWVectorIndex::~HNSWVectorIndex() = default;

void HNSWVectorIndex::insert(const ReelId &id, const Embedding &embedding) {
    // D2: validate dimension and finiteness ourselves BEFORE touching vector-db, so a vector-db-side
    // throw never occurs for these two cases in normal operation. Duplicate-key detection is
    // deliberately left to vector-db (adapters catch nothing on the hot path).
    if (embedding.size() != impl_->dimensions) {
        throw std::invalid_argument("HNSWVectorIndex::insert: embedding dimension " +
                                    std::to_string(embedding.size()) + " != index dimension " +
                                    std::to_string(impl_->dimensions));
    }
    for (float component : embedding) {
        if (!std::isfinite(component)) {
            throw std::invalid_argument(
                "HNSWVectorIndex::insert: embedding has a non-finite component (id " +
                std::to_string(id.value) + ")");
        }
    }

    // D4: ReelId -> decimal string key. Only the adapter touches string keys.
    ::Vector vec(embedding);
    impl_->index.insert(vec, std::to_string(id.value)); // dup-key throw (if any) propagates untouched
}

std::vector<VectorSearchResult> HNSWVectorIndex::search(const Embedding &query, size_t k) const {
    ::Vector q(query);
    const auto raw = impl_->index.search(q, k); // ascending distance; k==0/empty index => {}

    std::vector<VectorSearchResult> results;
    results.reserve(raw.size());
    for (const auto &[key, distance] : raw) {
        // D4: parse the decimal string key back to a ReelId. D3: distance -> cosine similarity.
        const ReelId reelId{static_cast<std::uint32_t>(std::stoul(key))};
        results.push_back(VectorSearchResult{reelId, distance, similarityFromEuclidean(distance)});
    }
    return results;
}

size_t HNSWVectorIndex::size() const {
    return impl_->index.size();
}

void HNSWVectorIndex::setEfSearch(size_t ef) {
    impl_->index.setEfSearch(ef);
}

std::vector<size_t> HNSWVectorIndex::getLevelDistribution() const {
    return impl_->index.getLevelDistribution();
}

} // namespace rr
