#include "gpu_trace.h"

#include <stdarg.h>
#include <unistd.h>

#include <chrono>
#include <mutex>

static std::atomic<uint64_t> g_pass_serial{1};
static std::atomic<uint64_t> g_target_generation{1};

struct RingEvent {
    uint64_t timestamp_us;
    char kind[32];
    char text[256];
};

static constexpr size_t kRingCapacity = 128;
static RingEvent g_ring[kRingCapacity];
static std::atomic<size_t> g_ring_head{0};
static std::mutex g_trace_mutex;

uint64_t gpu_trace_next_pass_serial() {
    return g_pass_serial.fetch_add(1, std::memory_order_relaxed);
}

uint64_t gpu_trace_next_target_generation() {
    return g_target_generation.fetch_add(1, std::memory_order_relaxed);
}

static uint64_t current_time_us() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

void gpu_trace_record_event(const char* kind, const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    size_t idx = g_ring_head.fetch_add(1, std::memory_order_relaxed) % kRingCapacity;
    g_ring[idx].timestamp_us = current_time_us();
    snprintf(g_ring[idx].kind, sizeof(g_ring[idx].kind), "%s", kind ? kind : "");
    snprintf(g_ring[idx].text, sizeof(g_ring[idx].text), "%s", buf);
}

void gpu_trace_rt_create(const char* kind, const char* name, uint32_t image_id, uint32_t tex_view_id,
                         uint32_t att_view_id, int width, int height, uint64_t generation) {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "[RT-TRACE] CREATE kind=%s name=\"%s\" img=%u tex_view=%u att_view=%u size=%dx%d gen=%lu\n",
            kind ? kind : "unknown", name ? name : "", image_id, tex_view_id, att_view_id, width, height,
            (unsigned long)generation);
    fflush(stderr);
    gpu_trace_record_event("RT_CREATE", "kind=%s name=%s img=%u tex_view=%u att_view=%u %dx%d gen=%lu",
                           kind ? kind : "", name ? name : "", image_id, tex_view_id, att_view_id, width, height,
                           (unsigned long)generation);
}

void gpu_trace_rt_destroy(const char* reason, const char* name, uint32_t image_id, uint32_t tex_view_id,
                          uint32_t att_view_id, int width, int height, uint64_t generation) {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "[RT-TRACE] DESTROY reason=%s name=\"%s\" img=%u tex_view=%u att_view=%u size=%dx%d gen=%lu\n",
            reason ? reason : "unknown", name ? name : "", image_id, tex_view_id, att_view_id, width, height,
            (unsigned long)generation);
    fflush(stderr);
    gpu_trace_record_event("RT_DESTROY", "reason=%s name=%s img=%u tex_view=%u att_view=%u %dx%d gen=%lu",
                           reason ? reason : "", name ? name : "", image_id, tex_view_id, att_view_id, width, height,
                           (unsigned long)generation);
}

static std::atomic<uint64_t> g_current_pass_serial{0};

void gpu_trace_set_current_pass_serial(uint64_t serial) {
    g_current_pass_serial.store(serial, std::memory_order_relaxed);
}

uint64_t gpu_trace_get_current_pass_serial() {
    return g_current_pass_serial.load(std::memory_order_relaxed);
}

void gpu_trace_pass_begin(const GpuPassTraceInfo& info) {
    gpu_trace_set_current_pass_serial(info.pass_serial);
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr,
            "[PASS-TRACE] BEGIN serial=%lu frame=%lu cat=%s layer=\"%s\"(id=%u) eff=%d(\"%s\") pass=%d "
            "shader=\"%s\" target=\"%s\"(scale=%.2f) size=%dx%d out_img=%u out_att=%u out_gen=%lu\n",
            (unsigned long)info.pass_serial, (unsigned long)info.frame_index, info.category ? info.category : "",
            info.layer_name ? info.layer_name : "", info.layer_id, info.effect_index,
            info.effect_path ? info.effect_path : "", info.pass_index, info.shader_name ? info.shader_name : "",
            info.render_target_name ? info.render_target_name : "", info.render_scale, info.target_width,
            info.target_height, info.output_image_id, info.output_view_id, (unsigned long)info.output_generation);

    for (const auto& in : info.inputs) {
        fprintf(stderr, "             -> in[%d] sem=\"%s\" img=%u view=%u gen=%lu is_rt=%s\n", in.slot,
                in.semantic.c_str(), in.image_id, in.view_id, (unsigned long)in.generation,
                in.is_render_target ? "yes" : "no");
    }
    fflush(stderr);

    gpu_trace_record_event("PASS_BEGIN", "serial=%lu cat=%s shader=%s out_img=%u out_att=%u",
                           (unsigned long)info.pass_serial, info.category ? info.category : "",
                           info.shader_name ? info.shader_name : "", info.output_image_id, info.output_view_id);
}

void gpu_trace_pass_end(uint64_t pass_serial) {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "[PASS-TRACE] END serial=%lu\n", (unsigned long)pass_serial);
    fflush(stderr);
    gpu_trace_record_event("PASS_END", "serial=%lu", (unsigned long)pass_serial);
}

void gpu_trace_bound_slots(uint64_t pass_serial, const sg_bindings* bind) {
    if (!bind) return;
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "[BIND-TRACE] pass_serial=%lu views=[", (unsigned long)pass_serial);
    for (int i = 0; i < 12; ++i) {
        if (bind->views[i].id != SG_INVALID_ID) {
            sg_view_desc vd = sg_query_view_desc(bind->views[i]);
            fprintf(stderr, "%d:(v=%u,img=%u) ", i, bind->views[i].id, vd.texture.image.id);
        }
    }
    fprintf(stderr, "] vb=[");
    for (int i = 0; i < SG_MAX_VERTEXBUFFER_BINDSLOTS; ++i) {
        if (bind->vertex_buffers[i].id != SG_INVALID_ID) {
            fprintf(stderr, "%d:b=%u ", i, bind->vertex_buffers[i].id);
        }
    }
    if (bind->index_buffer.id != SG_INVALID_ID) {
        fprintf(stderr, "] ib=b=%u\n", bind->index_buffer.id);
    } else {
        fprintf(stderr, "]\n");
    }
    fflush(stderr);
}

void gpu_trace_dump_history() {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "\n========== [GPU-TRACE RECENT EVENT HISTORY] ==========\n");
    size_t head = g_ring_head.load(std::memory_order_relaxed);
    size_t count = head < kRingCapacity ? head : kRingCapacity;
    size_t start = head < kRingCapacity ? 0 : (head % kRingCapacity);
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (start + i) % kRingCapacity;
        fprintf(stderr, "  [%lu us] %-12s %s\n", (unsigned long)g_ring[idx].timestamp_us, g_ring[idx].kind,
                g_ring[idx].text);
    }
    fprintf(stderr, "======================================================\n\n");
    fflush(stderr);
}

void gpu_trace_dump_history_signal_safe() {
    const char header[] = "\n=== [CRASH GPU-TRACE RECENT EVENT HISTORY] ===\n";
    (void)write(STDERR_FILENO, header, sizeof(header) - 1);
    size_t head = g_ring_head.load(std::memory_order_relaxed);
    size_t count = head < kRingCapacity ? head : kRingCapacity;
    size_t start = head < kRingCapacity ? 0 : (head % kRingCapacity);
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (start + i) % kRingCapacity;
        char line[512];
        int len = snprintf(line, sizeof(line), "  [%lu us] %s %s\n", (unsigned long)g_ring[idx].timestamp_us,
                           g_ring[idx].kind, g_ring[idx].text);
        if (len > 0) {
            (void)write(STDERR_FILENO, line, len);
        }
    }
    const char footer[] = "===============================================\n\n";
    (void)write(STDERR_FILENO, footer, sizeof(footer) - 1);
}
