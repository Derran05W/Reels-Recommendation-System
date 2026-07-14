#include "rr/infrastructure/process_stats.hpp"

#include <sys/resource.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

namespace rr {

uint64_t currentRssBytes() {
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                  &count) != KERN_SUCCESS) {
        return 0;
    }
    return info.resident_size;
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    uint64_t sizePages = 0;
    uint64_t residentPages = 0;
    if (!(statm >> sizePages >> residentPages)) {
        return 0;
    }
    return residentPages * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
#else
    return 0;
#endif
}

uint64_t peakRssBytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
#if defined(__APPLE__)
    // ru_maxrss is bytes on macOS.
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    // ru_maxrss is KiB on Linux.
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024;
#endif
}

CpuTimes processCpuTimes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return {};
    }
    auto toSeconds = [](const timeval &tv) {
        return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) / 1e6;
    };
    return {toSeconds(usage.ru_utime), toSeconds(usage.ru_stime)};
}

} // namespace rr
