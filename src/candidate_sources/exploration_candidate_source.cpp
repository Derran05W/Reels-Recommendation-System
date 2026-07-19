#include "rr/candidate_sources/exploration_candidate_source.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <utility>

#include "rr/core/embedding.hpp"
#include "rr/recommendation/effective_preference.hpp"

namespace rr {

namespace {

// See the other non-vector sources: a Candidate carrying the real cosine similarity (D3) and its
// D3-inverse distance so exploration reels are comparable at the Orchestrator's pool cap and feed
// the ranker's similarity feature.
Candidate makeCandidate(const Reel &reel, const Embedding &query) {
    Candidate candidate{};
    candidate.reelId = reel.id;
    candidate.source = CandidateSource::Exploration;
    const float sim = dot(query, reel.embedding);
    candidate.retrievalSimilarity = sim;
    candidate.retrievalDistance = std::sqrt(std::max(0.0f, 2.0f - 2.0f * sim));
    candidate.rankingScore = 0.0f;
    return candidate;
}

} // namespace

ExplorationCandidateSource::ExplorationCandidateSource(const std::vector<Reel> &reels,
                                                       double epsilon, uint32_t poolCap,
                                                       double freshWindowSeconds, Rng *rng,
                                                       double enableAtDay)
    : reels_(reels), epsilon_(epsilon), enableAtDay_(enableAtDay), poolCap_(poolCap),
      freshWindowSeconds_(freshWindowSeconds), rng_(rng) {}

std::vector<Candidate> ExplorationCandidateSource::generate(const User &user,
                                                            const RecommendationRequest &request) {
    std::vector<Candidate> candidates;

    // Contract step 1: a disabled request consumes NO rng and fires no slots.
    lastFiredSlots_ = 0;
    if (!request.enableExploration) {
        return candidates;
    }

    // Phase 21 time gate (contracts §1): before day enableAtDay_ the EFFECTIVE epsilon is 0. The
    // draw below still happens feedSize times regardless (step 2), and bernoulli(0) consumes the
    // same uniform01 as bernoulli(epsilon_), so ONLY the outcomes flip — the draw count and stream
    // alignment are identical to an ungated run, and the pre-day window reproduces epsilon=0
    // exactly. enableAtDay_ < 0 (the default) leaves epsilon_ untouched at all times (pre-P21
    // byte-identical). The day is floor(requestTime / 86400) computed in double so the unsigned
    // Timestamp never underflows.
    double effectiveEpsilon = epsilon_;
    if (enableAtDay_ >= 0.0 &&
        std::floor(static_cast<double>(request.requestTime) / 86400.0) < enableAtDay_) {
        effectiveEpsilon = 0.0;
    }

    // Contract step 2: draw EXACTLY feedSize per-slot gates, always, so the stream shape is
    // independent of epsilon and of the outcomes.
    std::size_t k = 0;
    for (std::size_t slot = 0; slot < request.feedSize; ++slot) {
        if (rng_->bernoulli(effectiveEpsilon)) {
            ++k;
        }
    }
    lastFiredSlots_ = k;

    // Contract step 3: no gate fired => empty, no further draws (epsilon = 0 lands here).
    if (k == 0) {
        return candidates;
    }

    // Contract step 4: build up to poolCap candidates across the three fixed-budget modes.
    const std::size_t cap = static_cast<std::size_t>(poolCap_);
    const std::size_t budgetA = cap / 3;                 // random-fresh
    const std::size_t budgetB = cap / 3;                 // underexposed
    const std::size_t budgetC = cap - budgetA - budgetB; // uncertain-topic (remainder of poolCap)

    const Embedding &query = effectivePreference(user);

    // Eligible = active + embeddable, in ascending ReelId (== ascending index, dense-id invariant).
    // Fresh pool = eligible within the recency window; cutoff computed in double so the unsigned
    // Timestamp never underflows.
    const double cutoff = static_cast<double>(request.requestTime) - freshWindowSeconds_;
    std::vector<uint32_t> eligible;
    std::vector<uint32_t> freshPool;
    for (std::size_t i = 0; i < reels_.size(); ++i) {
        const Reel &reel = reels_[i];
        if (!reel.active || reel.embedding.empty()) {
            continue;
        }
        eligible.push_back(static_cast<uint32_t>(i));
        if (static_cast<double>(reel.createdAt) >= cutoff) {
            freshPool.push_back(static_cast<uint32_t>(i));
        }
    }

    std::unordered_set<uint32_t> chosen;
    std::vector<uint32_t> selected;
    selected.reserve(cap);

    // Mode a — RANDOM-FRESH (the only rng consumer): partial Fisher-Yates over the fresh pool draws
    // exactly min(budgetA, |freshPool|) rng.uniformInt calls. freshPool starts in ascending ReelId
    // order, so the shuffle is reproducible for a given rng state.
    {
        std::vector<uint32_t> pool = freshPool;
        const std::size_t m = std::min(budgetA, pool.size());
        for (std::size_t i = 0; i < m; ++i) {
            const std::size_t j = i + static_cast<std::size_t>(rng_->uniformInt(pool.size() - i));
            std::swap(pool[i], pool[j]);
            if (chosen.insert(pool[i]).second) {
                selected.push_back(pool[i]);
            }
        }
    }

    // Mode b — UNDEREXPOSED (deterministic): lowest global impressionCount, ties by ascending
    // ReelId, up to budgetB, skipping reels already chosen.
    {
        std::vector<uint32_t> cands;
        cands.reserve(eligible.size());
        for (uint32_t idx : eligible) {
            if (chosen.find(idx) == chosen.end()) {
                cands.push_back(idx);
            }
        }
        const std::size_t m = std::min(budgetB, cands.size());
        std::partial_sort(cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(m),
                          cands.end(), [this](uint32_t a, uint32_t b) {
                              const uint64_t ia = reels_[a].impressionCount;
                              const uint64_t ib = reels_[b].impressionCount;
                              if (ia != ib) {
                                  return ia < ib;
                              }
                              return reels_[a].id.value < reels_[b].id.value;
                          });
        for (std::size_t i = 0; i < m; ++i) {
            chosen.insert(cands[i]);
            selected.push_back(cands[i]);
        }
    }

    // Mode c — UNCERTAIN-TOPIC (deterministic): embedding most DISTANT from the user's estimate
    // (lowest cosine), ties by ascending ReelId, up to budgetC, skipping reels already chosen.
    // Similarities are precomputed once so the sort does not repeat the O(dim) dot.
    {
        struct SimIdx {
            float sim;
            uint32_t idx;
        };
        std::vector<SimIdx> cands;
        cands.reserve(eligible.size());
        for (uint32_t idx : eligible) {
            if (chosen.find(idx) == chosen.end()) {
                cands.push_back(SimIdx{dot(query, reels_[idx].embedding), idx});
            }
        }
        const std::size_t m = std::min(budgetC, cands.size());
        std::partial_sort(cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(m),
                          cands.end(), [this](const SimIdx &a, const SimIdx &b) {
                              if (a.sim != b.sim) {
                                  return a.sim < b.sim;
                              }
                              return reels_[a.idx].id.value < reels_[b.idx].id.value;
                          });
        for (std::size_t i = 0; i < m; ++i) {
            chosen.insert(cands[i].idx);
            selected.push_back(cands[i].idx);
        }
    }

    candidates.reserve(selected.size());
    for (uint32_t idx : selected) {
        candidates.push_back(makeCandidate(reels_[idx], query));
    }
    return candidates;
}

} // namespace rr
