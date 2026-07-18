#include "rr/evaluation/welfare_metrics.hpp"

#include <cassert>

namespace rr {

// The simulated-watch-minute conversion (V2 TDD §4.9): watch time is measured in simulated
// seconds, satisfaction-per-minute divides by minutes.
namespace {
constexpr double kSecondsPerMinute = 60.0;
} // namespace

double satisfactionPerMinute(double satisfactionSum, double watchSecondsSum) {
    const double watchMinutes = watchSecondsSum / kSecondsPerMinute;
    return watchMinutes > 0.0 ? satisfactionSum / watchMinutes : 0.0;
}

WelfareMetrics::WelfareMetrics(std::size_t rounds, std::size_t archetypeCount)
    : rounds_(rounds), archetypes_(archetypeCount) {}

void WelfareMetrics::add(std::size_t round, const WelfareImpression &imp) {
    assert(round < rounds_.size() && "welfare round index out of range");
    RoundAcc &r = rounds_[round];
    r.impressions += 1;
    r.satisfactionSum += imp.immediateSatisfaction;
    r.regretSum += imp.regret;
    r.watchSecondsSum += imp.watchSeconds;

    // Per-archetype bucket (V2 §6 exposure breakdown). The generator keeps archetypeIndex in
    // [0, catalog size); guard against an out-of-range index rather than reach past the vector.
    assert(imp.archetypeIndex < archetypes_.size() && "archetype index out of range");
    if (imp.archetypeIndex < archetypes_.size()) {
        ArchAcc &a = archetypes_[imp.archetypeIndex];
        a.impressions += 1;
        a.satisfactionSum += imp.immediateSatisfaction;
        a.regretSum += imp.regret;
    }

    impressions_ += 1;
    satisfactionSum_ += imp.immediateSatisfaction;
    regretSum_ += imp.regret;
    watchSecondsSum_ += imp.watchSeconds;
}

WelfareReport WelfareMetrics::reduce(const std::vector<std::string> &archetypeNames) const {
    WelfareReport w;
    w.impressions = impressions_;
    w.meanSatisfaction =
        impressions_ > 0 ? satisfactionSum_ / static_cast<double>(impressions_) : 0.0;
    w.meanRegret = impressions_ > 0 ? regretSum_ / static_cast<double>(impressions_) : 0.0;
    w.satisfactionPerMinute = rr::satisfactionPerMinute(satisfactionSum_, watchSecondsSum_);
    w.watchMinutes = watchSecondsSum_ / kSecondsPerMinute;
    // Placeholders stay at their struct defaults (0.0); named here for intent.
    w.harmfulFatigue = 0.0;
    w.platformTrust = 0.0;

    w.byRound.reserve(rounds_.size());
    for (std::size_t r = 0; r < rounds_.size(); ++r) {
        const RoundAcc &acc = rounds_[r];
        WelfareRoundPoint p;
        p.round = r;
        p.impressions = acc.impressions;
        const double denom = acc.impressions > 0 ? static_cast<double>(acc.impressions) : 1.0;
        p.meanSatisfaction = acc.impressions > 0 ? acc.satisfactionSum / denom : 0.0;
        p.meanRegret = acc.impressions > 0 ? acc.regretSum / denom : 0.0;
        p.satisfactionPerMinute =
            rr::satisfactionPerMinute(acc.satisfactionSum, acc.watchSecondsSum);
        p.watchMinutes = acc.watchSecondsSum / kSecondsPerMinute;
        p.harmfulFatigue = 0.0;
        p.platformTrust = 0.0;
        w.byRound.push_back(p);
    }

    // Per-archetype exposure + welfare, one row per catalog archetype in index order
    // (deterministic; stable schema across arms even for zero-impression archetypes).
    w.byArchetype.reserve(archetypes_.size());
    for (std::size_t i = 0; i < archetypes_.size(); ++i) {
        const ArchAcc &acc = archetypes_[i];
        ArchetypeWelfare aw;
        aw.archetypeIndex = static_cast<uint32_t>(i);
        aw.name =
            i < archetypeNames.size() ? archetypeNames[i] : ("archetype_" + std::to_string(i));
        aw.impressions = acc.impressions;
        aw.exposureShare = impressions_ > 0 ? static_cast<double>(acc.impressions) /
                                                  static_cast<double>(impressions_)
                                            : 0.0;
        const double denom = acc.impressions > 0 ? static_cast<double>(acc.impressions) : 1.0;
        aw.meanSatisfaction = acc.impressions > 0 ? acc.satisfactionSum / denom : 0.0;
        aw.meanRegret = acc.impressions > 0 ? acc.regretSum / denom : 0.0;
        w.byArchetype.push_back(aw);
    }

    return w;
}

} // namespace rr
