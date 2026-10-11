#include "shared/core/system_stats.h"

#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace {
constexpr double kSampleIntervalSeconds = 1.0;
constexpr const char* kStatusPath = "/proc/self/status";
constexpr const char* kStatPath = "/proc/self/stat";
constexpr const char* kPciDevicesPath = "/sys/bus/pci/devices/";

bool startsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

// Status lines look like "VmRSS:    12345 kB". Returns the number.
uint64_t numberAfterColon(const std::string& line) {
    return std::strtoull(line.c_str() + line.find(':') + 1, nullptr, 10);
}

void readMemory(SystemStats& stats) {
    std::ifstream file(kStatusPath);
    std::string line;
    while (std::getline(file, line)) {
        if (startsWith(line, "VmRSS:")) {
            stats.rss_kb = numberAfterColon(line);
        } else if (startsWith(line, "VmHWM:")) {
            stats.rss_peak_kb = numberAfterColon(line);
        } else if (startsWith(line, "RssAnon:")) {
            stats.rss_anon_kb = numberAfterColon(line);
        } else if (startsWith(line, "RssFile:")) {
            stats.rss_file_kb = numberAfterColon(line);
        }
    }
}

// utime and stime, in clock ticks. The command name can contain spaces, so parse after the last ')'.
uint64_t cpuTicks() {
    std::ifstream file(kStatPath);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const size_t close = text.rfind(')');
    if (close == std::string::npos) return 0;
    std::istringstream fields(text.substr(close + 2));
    uint64_t utime = 0;
    uint64_t stime = 0;
    std::string token;
    // Field 3 (state) is the first token after ')'. utime is field 14 and stime is field 15.
    for (int field = 3; field <= 15 && fields >> token; ++field) {
        if (field == 14) utime = std::strtoull(token.c_str(), nullptr, 10);
        if (field == 15) stime = std::strtoull(token.c_str(), nullptr, 10);
    }
    return utime + stime;
}

bool readCounter(const std::string& path, uint64_t& value) {
    std::ifstream file(path);
    return static_cast<bool>(file >> value);
}

void readGpu(const std::string& pci_bus_id, SystemStats& stats) {
    const std::string base = std::string(kPciDevicesPath) + pci_bus_id + "/";
    stats.gpu_available = readCounter(base + "gpu_busy_percent", stats.gpu_busy_percent);
    if (!stats.gpu_available) return;
    readCounter(base + "mem_info_vram_used", stats.vram_used_bytes);
    readCounter(base + "mem_info_vram_total", stats.vram_total_bytes);
}
}  // namespace

SystemStatsSampler::SystemStatsSampler(std::string gpu_pci_bus_id) : gpu_pci_bus_id_(std::move(gpu_pci_bus_id)) {}

const SystemStats& SystemStatsSampler::sample(double now_seconds) {
    if (last_sample_seconds_ >= 0.0 && now_seconds - last_sample_seconds_ < kSampleIntervalSeconds) return stats_;

    readMemory(stats_);
    const uint64_t ticks = cpuTicks();
    if (last_sample_seconds_ >= 0.0) {
        const double elapsed = now_seconds - last_sample_seconds_;
        const double cpu_seconds = static_cast<double>(ticks - last_cpu_ticks_) / sysconf(_SC_CLK_TCK);
        stats_.cpu_percent = 100.0 * cpu_seconds / elapsed;
    }
    last_cpu_ticks_ = ticks;
    last_sample_seconds_ = now_seconds;

    stats_.gpu_available = false;
    if (!gpu_pci_bus_id_.empty()) readGpu(gpu_pci_bus_id_, stats_);
    return stats_;
}
