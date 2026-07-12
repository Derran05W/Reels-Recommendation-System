#pragma once

#include <cstddef>
#include <string>

#include "rr/domain/recommendation.hpp"

namespace rr {

class VectorIndex;

// TDD 16: top-level recommender. Concrete algorithms (random, popularity, HNSW, ...) arrive in
// later phases.
class Recommender {
  public:
    virtual RecommendationResponse recommend(const RecommendationRequest &request) = 0;

    virtual std::string name() const = 0;

    // The retrieval index this recommender queries, if it is vector-based; nullptr otherwise
    // (default). Evaluation-only hook (TDD 18.1): the harness measures live Recall@K / distance
    // error by querying this index against an exact ground-truth index on sampled requests. Never
    // used on the recommendation path itself.
    virtual const VectorIndex *retrievalIndex() const { return nullptr; }

    // Catalog-growth notification (Phase 8 mid-simulation reel injection, TDD 18.5): the harness
    // calls this after appending newly generated reels to the shared catalog vector;
    // `firstNewIndex` is the index of the first appended reel. Vector-based recommenders insert
    // the new ACTIVE reels into their retrieval index — insert-only, existing entries are never
    // updated or removed (D2 immutability holds). Default no-op: non-vector recommenders scan the
    // live catalog vector on every request and see appended reels automatically.
    virtual void onReelsAppended(size_t firstNewIndex) { (void)firstNewIndex; }

    virtual ~Recommender() = default;
};

} // namespace rr
