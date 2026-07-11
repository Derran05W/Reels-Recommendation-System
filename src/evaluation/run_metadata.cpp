#include "rr/evaluation/run_metadata.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/utsname.h>
#elif defined(__linux__)
#include <sys/utsname.h>
#include <unistd.h>
#endif

// Provenance collection mirrors apps/benchmark_retrieval.cpp (D12). The snippets below are the
// portable, vector-db-agnostic helpers from that harness; reusing them here pulls in NO vector-db
// header (D2 containment holds).

namespace rr {

namespace {

// Run a shell command, return trimmed stdout ("" on failure). Used only for git provenance --
// never on any measured/hot path.
std::string runCommand(const std::string &cmd) {
    std::string out;
    if (FILE *pipe = ::popen(cmd.c_str(), "r")) {
        char buf[512];
        while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
            out += buf;
        }
        ::pclose(pipe);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

std::string jsonEscape(const std::string &s) {
    std::string r;
    r.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':
            r += "\\\"";
            break;
        case '\\':
            r += "\\\\";
            break;
        case '\n':
            r += "\\n";
            break;
        case '\r':
            r += "\\r";
            break;
        case '\t':
            r += "\\t";
            break;
        default:
            r += c;
            break;
        }
    }
    return r;
}

std::string nowIso() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

#if defined(__APPLE__)
std::string sysctlString(const char *name) {
    size_t len = 0;
    if (sysctlbyname(name, nullptr, &len, nullptr, 0) != 0 || len == 0) {
        return "";
    }
    std::string buf(len, '\0');
    if (sysctlbyname(name, buf.data(), &len, nullptr, 0) != 0) {
        return "";
    }
    if (!buf.empty() && buf.back() == '\0') {
        buf.pop_back();
    }
    return buf;
}
uint64_t sysctlU64(const char *name) {
    uint64_t v = 0;
    size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, nullptr, 0) != 0) {
        return 0;
    }
    return v;
}
#endif

} // namespace

RunMetadata collectRunMetadata(const BuildProvenance &provenance, const std::string &experimentId) {
    RunMetadata meta;
    meta.experimentId = experimentId;
    meta.generatedAt = nowIso();

    meta.reelRankDir = provenance.repoDir;
    meta.vectorDbDir = provenance.vdbDir;
    meta.buildType = provenance.buildType;
    meta.compiler = provenance.compiler;

    meta.reelRankSha = runCommand("git -C '" + provenance.repoDir + "' rev-parse HEAD 2>/dev/null");
    meta.reelRankDirty =
        !runCommand("git -C '" + provenance.repoDir + "' status --porcelain 2>/dev/null").empty();
    meta.vectorDbSha = runCommand("git -C '" + provenance.vdbDir + "' rev-parse HEAD 2>/dev/null");

#if defined(__APPLE__)
    meta.cpuModel = sysctlString("machdep.cpu.brand_string");
    meta.ramBytes = sysctlU64("hw.memsize");
    meta.logicalCores = sysctlU64("hw.ncpu");
    meta.physicalCores = sysctlU64("hw.physicalcpu");
#endif

#if defined(__APPLE__) || defined(__linux__)
    struct utsname uts{};
    if (uname(&uts) == 0) {
        meta.osSysname = uts.sysname;
        meta.osRelease = uts.release;
        meta.osVersion = uts.version;
        meta.osMachine = uts.machine;
    }
#endif

    return meta;
}

void writeMetadataJson(const std::filesystem::path &dir, const RunMetadata &meta) {
    std::ofstream j(dir / "metadata.json");
    j << "{\n";
    j << "  \"experiment_id\": \"" << jsonEscape(meta.experimentId) << "\",\n";
    j << "  \"generated_at\": \"" << jsonEscape(meta.generatedAt) << "\",\n";
    j << "  \"tool\": \"simulate (ReelRank Phase 4 evaluation harness)\",\n";
    j << "  \"git\": {\n";
    j << "    \"reel_rank_sha\": \"" << jsonEscape(meta.reelRankSha) << "\",\n";
    j << "    \"reel_rank_dir\": \"" << jsonEscape(meta.reelRankDir) << "\",\n";
    j << "    \"reel_rank_dirty\": " << (meta.reelRankDirty ? "true" : "false") << ",\n";
    j << "    \"vector_db_sha\": \"" << jsonEscape(meta.vectorDbSha) << "\",\n";
    j << "    \"vector_db_dir\": \"" << jsonEscape(meta.vectorDbDir) << "\"\n";
    j << "  },\n";
    j << "  \"build\": {\n";
    j << "    \"type\": \"" << jsonEscape(meta.buildType) << "\",\n";
    j << "    \"compiler\": \"" << jsonEscape(meta.compiler) << "\",\n";
    j << "    \"cxx_standard\": 20\n";
    j << "  },\n";
    j << "  \"hardware\": {\n";
    j << "    \"cpu_model\": \"" << jsonEscape(meta.cpuModel) << "\",\n";
    j << "    \"logical_cores\": " << meta.logicalCores << ",\n";
    j << "    \"physical_cores\": " << meta.physicalCores << ",\n";
    j << "    \"ram_bytes\": " << meta.ramBytes << "\n";
    j << "  },\n";
    j << "  \"os\": {\n";
    j << "    \"sysname\": \"" << jsonEscape(meta.osSysname) << "\",\n";
    j << "    \"release\": \"" << jsonEscape(meta.osRelease) << "\",\n";
    j << "    \"version\": \"" << jsonEscape(meta.osVersion) << "\",\n";
    j << "    \"machine\": \"" << jsonEscape(meta.osMachine) << "\"\n";
    j << "  },\n";
    j << "  \"execution\": {\n";
    j << "    \"threads_used\": 1,\n";
    j << "    \"note\": \"single-threaded simulation core (D13)\"\n";
    j << "  },\n";
    j << "  \"dataset\": {\n";
    j << "    \"users\": " << meta.userCount << ",\n";
    j << "    \"reels\": " << meta.reelCount << ",\n";
    j << "    \"creators\": " << meta.creatorCount << ",\n";
    j << "    \"topics\": " << meta.topicCount << ",\n";
    j << "    \"dimensions\": " << meta.dimensions << "\n";
    j << "  }\n";
    j << "}\n";
}

} // namespace rr
