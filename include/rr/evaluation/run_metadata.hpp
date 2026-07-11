#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace rr {

// Build/repo facts the experiment cannot discover on its own — injected as compile definitions on
// the app/test target (RR_REPO_DIR, RR_VDB_DIR, RR_BUILD_TYPE, RR_COMPILER) and passed in as
// parameters (D12: the library stays free of build-time defines; the collectors take arguments).
struct BuildProvenance {
    std::string repoDir = "unknown";
    std::string vdbDir = "unknown";
    std::string buildType = "unknown";
    std::string compiler = "unknown";
};

// Everything metadata.json records for publication provenance (D12 / TDD 27). Git/hardware/OS
// facts are collected off any measured path; dataset facts are copied from the resolved config.
struct RunMetadata {
    std::string experimentId;
    std::string generatedAt; // ISO-8601 wall-clock stamp (provenance, not simulation time)

    std::string reelRankSha;
    std::string reelRankDir;
    bool reelRankDirty = false;
    std::string vectorDbSha;
    std::string vectorDbDir;

    std::string buildType;
    std::string compiler;

    std::string cpuModel;
    uint64_t ramBytes = 0;
    uint64_t logicalCores = 0;
    uint64_t physicalCores = 0;

    std::string osSysname;
    std::string osRelease;
    std::string osVersion;
    std::string osMachine;

    size_t userCount = 0;
    size_t reelCount = 0;
    size_t creatorCount = 0;
    size_t topicCount = 0;
    size_t dimensions = 0;
};

// Collect git provenance (SHAs + dirty flag via `git -C`), CPU/RAM/OS facts, and echo the build
// facts from `provenance`. Runs only off the timed path. Fields that cannot be discovered on this
// platform are left as "unknown"/0.
RunMetadata collectRunMetadata(const BuildProvenance &provenance, const std::string &experimentId);

// Serialize RunMetadata to <dir>/metadata.json (follows benchmark_retrieval.cpp's layout). This
// file carries wall-clock/hardware facts and is intentionally excluded from the determinism
// guarantee.
void writeMetadataJson(const std::filesystem::path &dir, const RunMetadata &meta);

} // namespace rr
