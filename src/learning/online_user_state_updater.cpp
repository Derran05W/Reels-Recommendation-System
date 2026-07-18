#include "rr/learning/online_user_state_updater.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <utility>

#include "rr/core/embedding.hpp"

namespace rr {

namespace {

// Degenerate-direction threshold. If a computed update target's L2 norm falls below this, its
// direction is meaningless (a near-perfect cancellation) and the caller applies a documented
// fallback instead of normalizing. Chosen comfortably above rr::normalize's 1e-12 hard floor, so
// any target that clears this epsilon is guaranteed to normalize without throwing.
constexpr double kDegenerateNormEpsilon = 1e-6;

// L2-normalize `v` in place iff its direction is well-defined. Returns true on success (v is now
// unit-length); returns false and leaves `v` untouched when the norm is below
// kDegenerateNormEpsilon (the "degenerate" case), signalling the caller to apply its own fallback.
// The norm is accumulated in double to match rr::normalize.
bool tryNormalize(Embedding &v) {
    double sumSq = 0.0;
    for (float x : v) {
        sumSq += static_cast<double>(x) * static_cast<double>(x);
    }
    if (std::sqrt(sumSq) < kDegenerateNormEpsilon) {
        return false;
    }
    normalize(v); // safe: norm >= kDegenerateNormEpsilon >> 1e-12; inputs are finite
    return true;
}

// Realism V2 gated per-modality estimate update (Phase 15, V2 TDD 5), mirroring the TDD 11.2
// long-term rule applied to ONE modality embedding, driven by the SAME observable reward `r`:
//   est <- normalize((1 - eta) * est + eta * r * modality).
// No-op when the reel carries no such modality embedding (`modality.empty()` — the gate-off default
// on the reel, or a defensive guard). COLD START: an EMPTY estimate is seeded to the reel's
// modality DIRECTION (unit-normalized — the first observation, mirroring 11.1's cold-start of the
// long-term vector to a prior; there is no modality prior to average, so the first observation IS
// the seed). A degenerate EMA target (near-cancellation, only reachable with an adversarial eta)
// keeps the previous estimate, matching the V1 rules' fallback doctrine. A dimension mismatch (a
// pipeline bug — all embeddings share simulation.dimensions once seeded) is skipped rather than
// read out of range.
void updateModalityEstimate(Embedding &est, const Embedding &modality, double eta, double r) {
    if (modality.empty()) {
        return;
    }
    if (est.empty()) {
        est = modality;    // seed to the first observed modality direction...
        tryNormalize(est); // ...as a unit vector (a valid modality embedding is already unit-norm)
        return;
    }
    if (est.size() != modality.size()) {
        return;
    }
    const std::size_t dim = est.size();
    Embedding target(dim, 0.0f);
    for (std::size_t i = 0; i < dim; ++i) {
        const double e = static_cast<double>(est[i]);
        const double v = static_cast<double>(modality[i]);
        target[i] = static_cast<float>((1.0 - eta) * e + eta * r * v);
    }
    if (tryNormalize(target)) {
        est = std::move(target);
    }
}

} // namespace

OnlineUserStateUpdater::OnlineUserStateUpdater(const std::vector<Reel> &reels,
                                               const LearningConfig &config, bool contentV2)
    : reels_(reels), config_(config), contentV2_(contentV2) {}

void OnlineUserStateUpdater::apply(User &user, const Reel &reel,
                                   const InteractionEvent &interaction) const {
    // Master switch (LearningConfig.enabled): when learning is disabled the updater is a strict
    // no-op, freezing every user at its cold-start estimate (the pre-Phase-7 behaviour, and the
    // "frozen estimates" experiment arm). Documented on the config field.
    if (!config_.enabled) {
        return;
    }

    const std::size_t dim = reel.embedding.size();
    // Pipeline invariant (D5): every embedding shares the configured dimension. The three
    // preference vectors are cold-started to that dimension and only ever replaced by same-dim
    // vectors here, so this holds by construction on the hot path (assert in Debug, not a throw).
    assert(user.longTermPreference.size() == dim);
    assert(user.sessionPreference.size() == dim);
    assert(user.estimatedPreference.size() == dim);

    // 1. Long-term update (TDD 11.2): u <- normalize((1 - eta) * u + eta * r * v). eta is the
    //    long-term learning rate, r the (already [-1,1]-clamped) reward, v the reel embedding.
    //    Positive reward pulls u toward v; negative reward pushes it away.
    //    Fallback: if the target's norm is degenerate (a near-cancellation, only reachable with an
    //    adversarial eta since |eta*r*v| <= eta and ||(1-eta)*u|| = 1-eta keep ||target|| >=
    //    1-2*eta for the default eta=0.02, i.e. >= 0.96), keep the previous long-term vector
    //    unchanged.
    {
        const double eta = config_.longTermRate;
        const double r = static_cast<double>(interaction.reward);
        Embedding target(dim, 0.0f);
        for (std::size_t i = 0; i < dim; ++i) {
            const double u = static_cast<double>(user.longTermPreference[i]);
            const double v = static_cast<double>(reel.embedding[i]);
            target[i] = static_cast<float>((1.0 - eta) * u + eta * r * v);
        }
        if (tryNormalize(target)) {
            user.longTermPreference = std::move(target);
        }
    }

    // 2. Session recompute (TDD 11.3): s <- normalize(sum over the recent-interaction window's
    //    CURRENT-session events i of lambda^(n-i) * r_i * v_i), where events are chronological
    //    (deque back == newest), i = 1..n over the current-session events, so the NEWEST such event
    //    gets weight lambda^0 = 1 and older ones decay by lambda. Restricting the sum to events
    //    sharing this interaction's sessionId makes the between-session reset implicit: a session
    //    rotation changes the id, so the previous session's events drop out and the sum restarts.
    //    Embeddings of past in-window events are looked up from the dense-id catalog
    //    (reels_[reelId.value]). This is O(recentWindow * dim); nothing scales with the catalog.
    //    Fallback: if the sum's norm is degenerate (rewards all ~0, or opposing rewards on
    //    collinear reels cancelling), fall back to the (just-updated) long-term vector.
    {
        const double lambda = config_.sessionLambda;
        const SessionId session = interaction.sessionId;
        std::vector<double> acc(dim, 0.0);
        double weight = 1.0; // lambda^0 for the newest current-session event
        for (auto it = user.recentInteractions.rbegin(); it != user.recentInteractions.rend();
             ++it) {
            if (it->sessionId != session) {
                continue; // other-session events are excluded and consume no exponent
            }
            const Embedding &v = reels_[it->reelId.value].embedding;
            const double coeff = weight * static_cast<double>(it->reward);
            for (std::size_t i = 0; i < dim; ++i) {
                acc[i] += coeff * static_cast<double>(v[i]);
            }
            weight *= lambda;
        }
        Embedding sessionVec(dim, 0.0f);
        for (std::size_t i = 0; i < dim; ++i) {
            sessionVec[i] = static_cast<float>(acc[i]);
        }
        if (tryNormalize(sessionVec)) {
            user.sessionPreference = std::move(sessionVec);
        } else {
            user.sessionPreference = user.longTermPreference;
        }
    }

    // 3. Effective blend (TDD 8.3): estimated <- normalize(longTermWeight * longTerm +
    //    sessionWeight * session), computed from the vectors just updated above. This keeps
    //    estimatedPreference the CACHED effective preference, so rr::effectivePreference(user)
    //    stays a const-ref return. Fallback: if the blend degenerates (only reachable adversarially
    //    - both weights positive and the two unit vectors near-antipodal, e.g. equal weights with
    //    opposite directions), keep the previous estimate unchanged.
    {
        const double wl = config_.longTermWeight;
        const double ws = config_.sessionWeight;
        Embedding est(dim, 0.0f);
        for (std::size_t i = 0; i < dim; ++i) {
            const double lt = static_cast<double>(user.longTermPreference[i]);
            const double se = static_cast<double>(user.sessionPreference[i]);
            est[i] = static_cast<float>(wl * lt + ws * se);
        }
        if (tryNormalize(est)) {
            user.estimatedPreference = std::move(est);
        }
    }

    // 4. Realism V2 gated per-modality estimates (Phase 15, V2 TDD 5): mirror the 11.2 rule on the
    //    reel's THREE modality embeddings at modalityRate, driven by the SAME observable reward.
    //    Only when contentV2_ (realism.content_v2) — gate-off leaves the estimate vectors untouched
    //    (empty), so this whole block is a no-op and V1 behaviour is byte-identical (D17). Each
    //    modality is guarded by its own reel embedding being present (they are populated together
    //    by augmentReelsV2, absent under gate-off). estimated* live ONLY on User
    //    (recommender-visible, D18); the semantic candidate query is untouched (D23).
    if (contentV2_) {
        const double etaM = config_.modalityRate;
        const double r = static_cast<double>(interaction.reward);
        updateModalityEstimate(user.estimatedVisualPreference, reel.visualStyleEmbedding, etaM, r);
        updateModalityEstimate(user.estimatedMusicPreference, reel.musicEmbedding, etaM, r);
        updateModalityEstimate(user.estimatedEmotionalPreference, reel.emotionalToneEmbedding, etaM,
                               r);
    }
}

} // namespace rr
