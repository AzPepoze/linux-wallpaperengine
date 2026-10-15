#pragma once

#include <cstdint>
#include <string>

// Process memory and CPU, plus GPU load and VRAM from the amdgpu sysfs counters.
struct SystemStats {
    uint64_t rss_kb = 0;
    uint64_t rss_peak_kb = 0;
    uint64_t rss_anon_kb = 0;
    uint64_t rss_file_kb = 0;
    // Percent of one core since the previous sample. 100 means one core fully busy.
    double cpu_percent = 0.0;
    bool gpu_available = false;
    uint64_t gpu_busy_percent = 0;
    uint64_t vram_used_bytes = 0;
    uint64_t vram_total_bytes = 0;
};

class SystemStatsSampler {
   public:
    explicit SystemStatsSampler(std::string gpu_pci_bus_id);

    // Reads the counters at most once per second. Cheap to call every frame.
    const SystemStats& sample(double now_seconds);

   private:
    std::string gpu_pci_bus_id_;
    SystemStats stats_;
    double last_sample_seconds_ = -1.0;
    uint64_t last_cpu_ticks_ = 0;
};
