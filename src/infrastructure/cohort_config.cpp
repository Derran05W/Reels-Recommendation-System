#include "rr/infrastructure/cohort_config.hpp"

#include <array>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace rr {

namespace {

// Parse + validate the optional override at key `key`. Absent => leave `out` default (present ==
// false). Present must be a 2-element array [lo, hi] of numbers with 0 <= lo <= hi <= 1 (every
// overridable trait is a [0, 1] userTraitsV2 range).
void readOverride(const nlohmann::json &j, const char *key, const std::string &cohortName,
                  TraitOverride &out) {
    if (!j.contains(key)) {
        return;
    }
    const auto &v = j.at(key);
    if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) {
        throw std::invalid_argument("trait-cohort '" + cohortName + "' override '" + key +
                                    "' must be a [lo, hi] pair of numbers");
    }
    const double lo = v[0].get<double>();
    const double hi = v[1].get<double>();
    if (!(lo >= 0.0 && lo <= hi && hi <= 1.0)) {
        throw std::invalid_argument("trait-cohort '" + cohortName + "' override '" + key +
                                    "' must satisfy 0 <= lo <= hi <= 1");
    }
    out = TraitOverride{lo, hi, true};
}

// Emit a present override as a [lo, hi] array; absent overrides are omitted so an override-free
// spec round-trips through to_json/from_json back to an override-free spec (defaulted operator==).
void writeOverride(nlohmann::json &j, const char *key, const TraitOverride &ov) {
    if (ov.present) {
        j[key] = nlohmann::json::array({ov.lo, ov.hi});
    }
}

} // namespace

void to_json(nlohmann::json &j, const TraitCohortSpec &c) {
    j = nlohmann::json{{"name", c.name}, {"weight", c.weight}};
    writeOverride(j, "repetition_tolerance", c.repetitionTolerance);
    writeOverride(j, "novelty_tolerance", c.noveltyTolerance);
    writeOverride(j, "creator_loyalty", c.creatorLoyalty);
    writeOverride(j, "novelty_seeking", c.noveltySeeking);
}

void from_json(const nlohmann::json &j, TraitCohortSpec &c) {
    static constexpr std::array<const char *, 6> kKnownKeys{
        "name",           "weight", "repetition_tolerance", "novelty_tolerance", "creator_loyalty",
        "novelty_seeking"};
    for (const auto &[key, value] : j.items()) {
        (void)value;
        bool known = false;
        for (const char *k : kKnownKeys) {
            known = known || key == k;
        }
        if (!known) {
            throw std::invalid_argument("unknown trait-cohort key: " + key);
        }
    }
    if (!j.contains("name") || !j.at("name").is_string() ||
        j.at("name").get<std::string>().empty()) {
        throw std::invalid_argument("trait cohort requires a non-empty name");
    }
    c.name = j.at("name").get<std::string>();
    c.weight = j.value("weight", 1.0);
    if (!(c.weight > 0.0)) {
        throw std::invalid_argument("trait-cohort weight must be > 0 for " + c.name);
    }
    readOverride(j, "repetition_tolerance", c.name, c.repetitionTolerance);
    readOverride(j, "novelty_tolerance", c.name, c.noveltyTolerance);
    readOverride(j, "creator_loyalty", c.name, c.creatorLoyalty);
    readOverride(j, "novelty_seeking", c.name, c.noveltySeeking);
}

TraitCohortSpec namedCohort(const std::string &name) {
    TraitCohortSpec c;
    c.name = name;
    c.weight = 1.0;
    if (name == "focused") {
        // Concentration lovers: tolerate repetition, do not chase novelty. Slow topic/format/music
        // fatigue drag (high repetitionTolerance) => satisfaction declines slowly on repeats.
        c.repetitionTolerance = TraitOverride{0.8, 1.0, true};
        c.noveltySeeking = TraitOverride{0.0, 0.3, true};
    } else if (name == "novelty_seeker") {
        // Crave new content, tire of repeats fast (low repetitionTolerance => fast repetition
        // fatigue), but wear down slowly on GENERAL scrolling fatigue (high noveltyTolerance).
        c.noveltySeeking = TraitOverride{0.7, 1.0, true};
        c.repetitionTolerance = TraitOverride{0.0, 0.3, true};
        c.noveltyTolerance = TraitOverride{0.7, 1.0, true};
    } else if (name == "creator_loyal") {
        // Loyal to favoured creators: creator-fatigue immunity (high creatorLoyalty => small
        // creator-fatigue satisfaction penalty on same-creator runs).
        c.creatorLoyalty = TraitOverride{0.8, 1.0, true};
    } else if (name == "easily_fatigued") {
        // Wear down on everything: fast repetition fatigue (low repetitionTolerance) AND fast
        // general scrolling fatigue (low noveltyTolerance, wired into the general-fatigue drag) —
        // the double hit makes this cohort decline fastest of the four on a repetitive feed.
        c.repetitionTolerance = TraitOverride{0.0, 0.2, true};
        c.noveltyTolerance = TraitOverride{0.0, 0.3, true};
    } else {
        throw std::invalid_argument("unknown named cohort: " + name);
    }
    return c;
}

} // namespace rr
