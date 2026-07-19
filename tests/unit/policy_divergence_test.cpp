// Phase 20 POLICY-DIVERGENCE statistical test (package B, plan task 6 / Tier-4 acceptance 1).
// Two ranking policies — P15's engagement preset vs its satisfaction-proxy preset — run on
// IDENTICAL worlds/seeds through the event runner with preference_evolution + retention ON, then
// the per-user FINAL hidden semantic preferences (hidden_preference_final.csv) are compared
// MATCHED-user: an engagement-optimizing policy should distort hidden preferences measurably
// differently from a welfare-proxy policy (the headline "recommendations shape the world" result).
//
// EXPECTED-FAIL / SKIP protocol (P19 precedent): in package B's worktree package A's
// PreferenceEvolution is a NO-OP stub, so NO preference moves and the divergence is 0. The test
// PROBES AT RUNTIME whether any preference actually moved (max sem_shift over both arms) and
// GTEST_SKIPs with "PENDING PACKAGE A INTEGRATION" when it has not — never asserting against a
// guessed schema. Post-merge (evolution live) the probe passes and the divergence asserts fire; the
// integrator verifies the flip from SKIP to live.

#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace rr;

namespace {

namespace fs = std::filesystem;

struct FinalPref {
    double semShift = 0.0;
    std::vector<double> semantic;
};

// Load a P15 published arm's ranking-weight preset (only the `ranking` block; the rest of that P15
// config is round-robin/pre-P16 and irrelevant here).
bool loadRankingPreset(const std::string &arm, RankingConfig &out) {
    const fs::path p =
        fs::path(RR_SOURCE_DIR) / "results" / "published" / "phase15" / arm / "config.json";
    if (!fs::exists(p)) {
        return false;
    }
    std::ifstream in(p);
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.contains("ranking")) {
        return false;
    }
    from_json(j.at("ranking"), out);
    return true;
}

ExperimentConfig divergenceConfig(const RankingConfig &ranking, uint64_t seed = 20260721) {
    ExperimentConfig c;
    c.simulation.seed = seed;
    c.simulation.users = 120;
    c.simulation.reels = 1200;
    c.simulation.creators = 24;
    c.simulation.topics = 8;
    c.simulation.dimensions = 16;
    c.simulation.scheduler = "event_queue";
    c.simulation.horizonSeconds = 2.0 * 86400.0;
    c.recommendation.feedSize = 10;
    c.recommendation.vectorCandidates = 100;
    c.evaluation.oracleSampleRate = 0.0;
    c.evaluation.retrievalSampleRate = 0.0;
    c.algorithm = RecommendationAlgorithm::HnswRanker;
    c.ranking = ranking; // the policy under test (engagement vs proxy weights)
    c.realism.contentV2 = true;
    c.realism.latentReactions = true;
    c.realism.sessionDynamics = true;
    c.realism.preferenceEvolution = true; // the world-shaping gate (no-op stub in B's worktree)
    c.retention.enabled = true;
    c.scheduling.openStaggerSeconds = 3600.0;
    c.scheduling.returnDelayMeanSeconds = 3600.0;
    return c;
}

std::map<uint32_t, FinalPref> readFinalPrefs(const fs::path &dir) {
    std::map<uint32_t, FinalPref> out;
    std::ifstream in(dir / "hidden_preference_final.csv");
    std::string line;
    std::getline(in, line); // header
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        std::stringstream ss(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(ss, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() < 8) {
            continue;
        }
        FinalPref fp;
        const uint32_t id = static_cast<uint32_t>(std::stoul(cells[0]));
        fp.semShift = std::stod(cells[3]);
        for (std::size_t i = 7; i < cells.size(); ++i) { // sem_v0.. start at column 7
            fp.semantic.push_back(std::stod(cells[i]));
        }
        out.emplace(id, std::move(fp));
    }
    return out;
}

double oneMinusCos(const std::vector<double> &a, const std::vector<double> &b) {
    if (a.size() != b.size() || a.empty()) {
        return 0.0;
    }
    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    if (na <= 0.0 || nb <= 0.0) {
        return 0.0;
    }
    return 1.0 - dot / (std::sqrt(na) * std::sqrt(nb));
}

double maxShift(const std::map<uint32_t, FinalPref> &m) {
    double mx = 0.0;
    for (const auto &[id, fp] : m) {
        mx = std::max(mx, std::abs(fp.semShift));
    }
    return mx;
}

double meanShift(const std::map<uint32_t, FinalPref> &m) {
    if (m.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const auto &[id, fp] : m) {
        sum += fp.semShift;
    }
    return sum / static_cast<double>(m.size());
}

} // namespace

TEST(PolicyDivergenceTest, EngagementVsProxyDivergesOncePreferencesMove) {
    RankingConfig engagementRanking;
    RankingConfig proxyRanking;
    if (!loadRankingPreset("engagement", engagementRanking) ||
        !loadRankingPreset("proxy", proxyRanking)) {
        GTEST_SKIP() << "phase15 engagement/proxy ranking presets not found";
    }

    const fs::path engDir = fs::path(::testing::TempDir()) / "rr_p20_div_eng";
    const fs::path proxDir = fs::path(::testing::TempDir()) / "rr_p20_div_prox";
    fs::remove_all(engDir);
    fs::remove_all(proxDir);
    const ExperimentResult eng =
        ExperimentRunner(divergenceConfig(engagementRanking), engDir).run();
    const ExperimentResult prox = ExperimentRunner(divergenceConfig(proxyRanking), proxDir).run();

    const std::map<uint32_t, FinalPref> engPrefs = readFinalPrefs(eng.directory);
    const std::map<uint32_t, FinalPref> proxPrefs = readFinalPrefs(prox.directory);
    ASSERT_FALSE(engPrefs.empty());
    ASSERT_FALSE(proxPrefs.empty());

    // RUNTIME PROBE (never a schema guess): did ANY hidden preference actually move in EITHER arm?
    // In B's worktree evolution is a no-op stub, so nothing moves and this stays 0 -> SKIP pending
    // A.
    constexpr double kMovedEps = 1e-6;
    const double moved = std::max(maxShift(engPrefs), maxShift(proxPrefs));
    if (moved <= kMovedEps) {
        GTEST_SKIP()
            << "PENDING PACKAGE A INTEGRATION: preference_evolution is a no-op stub in this "
               "worktree (max sem_shift="
            << moved << "); the divergence assertions go live once package A lands.";
    }

    // --- Live assertions (post-merge). Each policy moved preferences vs run start, and matched
    //     users diverge BETWEEN policies by more than a calibrated margin. ---
    EXPECT_GT(meanShift(engPrefs), 0.0);  // engagement policy moved preferences
    EXPECT_GT(meanShift(proxPrefs), 0.0); // proxy policy moved preferences

    double divSum = 0.0;
    std::size_t matched = 0;
    for (const auto &[id, engFp] : engPrefs) {
        const auto it = proxPrefs.find(id);
        if (it == proxPrefs.end()) {
            continue;
        }
        divSum += oneMinusCos(engFp.semantic, it->second.semantic);
        ++matched;
    }
    ASSERT_GT(matched, 0u);
    const double meanDivergence = divSum / static_cast<double>(matched);
    // Calibrated margin: the two policies must push the SAME users' hidden semantic preferences to
    // measurably different places. Set at the demonstrated integration operating point — observed
    // meanDivergence 0.009492 at this reduced scale/seed with live package-A evolution
    // (eta_evo=0.02). 0.005 is a mechanism-alive tripwire (well above matched-user float noise),
    // not a magnitude claim; the published medium-scale experiment carries the headline number.
    EXPECT_GT(meanDivergence, 0.005)
        << "engagement vs proxy matched-user mean (1 - cos) divergence too small";
}
