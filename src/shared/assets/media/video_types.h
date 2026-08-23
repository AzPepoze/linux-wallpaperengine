#pragma once

#include <va/va.h>
#include <va/va_drmcommon.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

struct ZeroCopyMetrics {
    uint64_t sws_scale_calls = 0;
    uint64_t hwframe_cpu_transfers = 0;
    uint64_t cpu_rgba_bytes = 0;
    uint64_t cpu_video_upload_bytes = 0;
    uint64_t sg_update_image_bytes = 0;
    uint64_t vaapi_frames_decoded = 0;
    uint64_t dmabuf_exports = 0;
    uint64_t vulkan_imports_created = 0;
    uint64_t import_cache_hits = 0;
    uint64_t import_cache_misses = 0;
};

struct PlaybackStats {
    uint64_t packets_read = 0;
    uint64_t frames_decoded = 0;
    uint64_t unique_frames_presented = 0;
    uint64_t swapchain_frames = 0;
    uint64_t repeated_frames = 0;
    uint64_t dropped_frames = 0;
    uint64_t late_frames = 0;
    uint32_t loop_count = 0;
    double max_lateness_ms = 0.0;
    double sum_lateness_ms = 0.0;
    double current_pts_sec = 0.0;
    double playback_clock_sec = 0.0;
    double av_timing_error_ms = 0.0;
};

struct PerformanceTiming {
    double demux_cpu_ms = 0.0;
    double decode_submit_cpu_ms = 0.0;
    double va_sync_cpu_ms = 0.0;
    double dmabuf_export_cpu_ms = 0.0;
    double import_cache_cpu_ms = 0.0;
    double command_record_cpu_ms = 0.0;
    double scheduler_cpu_ms = 0.0;
    double video_gpu_ms = 0.0;
    double cpu_frame_ms = 0.0;
};
