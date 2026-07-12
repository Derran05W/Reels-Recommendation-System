#include "rr/candidate_sources/fresh_candidate_source.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "rr/core/embedding.hpp"
#include "rr/recommendation/effective_preference.hpp"

namespace rr {

namespace {

// See PopularCandidateSource: fill a non-vector Candidate with the real cosine similarity (D3) and
// its D3-inverse distance so every source is comparable at the Orchestrator's pool cap.
Candidate makeCandidate(const Reel &reel, CandidateSource source, const Embedding &query) {
    Candidate candidate{};
    candidate.reelId = reel.id;
    candidate.source = source;
    const float sim = dot(query, reel.embedding);
    candidate.retrievalSimilarity = sim;
    candidate.retrievalDistance = std::sqrt(std::max(0.0f, 2.0f - 2.0f * sim));
    candidate.rankingScore = 0.0f;
    return candidate;
}

} // namespace

FreshCandidateSource::FreshCandidateSource(const std::vector<Reel> &reels, uint32_t count,
                                           double freshWindowSeconds, bool topicProximity)
    : reels_(reels), count_(count), freshWindowSeconds_(freshWindowSeconds),
      topicProximity_(topicProximity) {}

std::vector<Candidate> FreshCandidateSource::generate(const User &user,
                                                      const RecommendationRequest &request) {
    std::vector<Candidate> candidates;
    if (count_ == 0) {
        return candidates;
    }

    // Recency lower bound in double so the unsigned Timestamp never underflows (a window wider than
    // `t` yields a non-positive cutoff, admitting every reel). request.requestTime is the logical
    // `now`.
    const double cutoff = static_cast<double>(request.requestTime) - freshWindowSeconds_;
    const Embedding &query = effectivePreference(user);

    // One qualifying scan into the reused scratch buffer: active, embeddable, within the window,
    // and (optionally) near the user's estimated interests.
    scratch_.clear();
    for (std::size_t i = 0; i < reels_.size(); ++i) {
        const Reel &reel = reels_[i];
        if (!reel.active || reel.embedding.empty()) {
            continue;
        }
        if (static_cast<double>(reel.createdAt) < cutoff) {
            continue;
        }
        if (topicProximity_ && dot(query, reel.embedding) < kTopicProximityMinSimilarity) {
            continue;
        }
        scratch_.push_back(Scored{reel.createdAt, static_cast<uint32_t>(i)});
    }

    // Deterministic total order: createdAt DESCENDING (newest first), ties by ascending ReelId.
    // partial_sort selects just the top-n; may yield fewer than `count`.
    const std::size_t n = std::min(static_cast<std::size_t>(count_), scratch_.size());
    std::partial_sort(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(n),
                      scratch_.end(), [this](const Scored &a, const Scored &b) {
                          if (a.createdAt != b.createdAt) {
                              return a.createdAt > b.createdAt;
                          }
                          return reels_[a.index].id.value < reels_[b.index].id.value;
                      });

    candidates.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        candidates.push_back(
            makeCandidate(reels_[scratch_[i].index], CandidateSource::Fresh, query));
    }
    return candidates;
}

} // namespace rr
