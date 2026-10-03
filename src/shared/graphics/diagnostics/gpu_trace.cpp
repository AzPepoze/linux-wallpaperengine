#include "gpu_trace.h"

#include <stdarg.h>
#include <unistd.h>

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

static std::atomic<uint64_t> g_pass_serial{1};
static std::atomic<uint64_t> g_target_generation{1};
static std::atomic<size_t> g_frame_cumulative_uniform_bytes{0};

static std::vector<uint64_t> g_pass_stack;
static std::mutex g_pass_stack_mutex;

uint64_t gpu_trace_next_pass_serial() {
    return g_pass_serial.fetch_add(1, std::memory_order_relaxed);
}

uint64_t gpu_trace_next_target_generation() {
    return g_target_generation.fetch_add(1, std::memory_order_relaxed);
}

void gpu_trace_set_current_pass_serial(uint64_t serial) {
    std::lock_guard<std::mutex> lock(g_pass_stack_mutex);
    if (serial == 0) {
        g_pass_stack.clear();
    } else {
        g_pass_stack.push_back(serial);
    }
}

uint64_t gpu_trace_get_current_pass_serial() {
    std::lock_guard<std::mutex> lock(g_pass_stack_mutex);
    return g_pass_stack.empty() ? 0 : g_pass_stack.back();
}

struct RingEvent {
    size_t length = 0;
    char text[512] = {};
};

static constexpr size_t kRingCapacity = 128;
static RingEvent g_ring[kRingCapacity];
static std::atomic<size_t> g_ring_head{0};
static std::mutex g_trace_mutex;

static uint64_t current_time_us() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

void gpu_trace_record_event(const char* kind, const char* fmt, ...) {
    char payload[384];
    va_list args;
    va_start(args, fmt);
    vsnprintf(payload, sizeof(payload), fmt, args);
    va_end(args);

    uint64_t ts = current_time_us();
    size_t idx = g_ring_head.fetch_add(1, std::memory_order_relaxed) % kRingCapacity;

    int len = snprintf(g_ring[idx].text, sizeof(g_ring[idx].text), "  [%lu us] %-14s %s\n", (unsigned long)ts,
                       kind ? kind : "", payload);
    g_ring[idx].length = (len > 0 && (size_t)len < sizeof(g_ring[idx].text)) ? (size_t)len : 0;
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

    gpu_trace_register_image(image_id, kind, name, width, height, generation);
    gpu_trace_register_view(tex_view_id, image_id, kind, name, generation);
    gpu_trace_register_view(att_view_id, image_id, kind, name, generation);
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

    gpu_trace_unregister_view(tex_view_id, reason);
    gpu_trace_unregister_view(att_view_id, reason);
    gpu_trace_unregister_image(image_id, reason);
}

void gpu_trace_pass_begin(const GpuPassTraceInfo& info) {
    {
        std::lock_guard<std::mutex> stack_lock(g_pass_stack_mutex);
        g_pass_stack.push_back(info.pass_serial);
    }
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
    {
        std::lock_guard<std::mutex> stack_lock(g_pass_stack_mutex);
        if (!g_pass_stack.empty()) {
            if (g_pass_stack.back() == pass_serial) {
                g_pass_stack.pop_back();
            } else {
                for (auto it = g_pass_stack.rbegin(); it != g_pass_stack.rend(); ++it) {
                    if (*it == pass_serial) {
                        g_pass_stack.erase(std::next(it).base());
                        break;
                    }
                }
            }
        }
    }
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "[PASS-TRACE] END serial=%lu\n", (unsigned long)pass_serial);
    fflush(stderr);
    gpu_trace_record_event("PASS_END", "serial=%lu", (unsigned long)pass_serial);
}

void gpu_trace_frame_commit_begin(uint64_t frame_index) {
    g_frame_cumulative_uniform_bytes.store(0, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "[FRAME-TRACE] COMMIT-BEGIN frame=%lu\n", (unsigned long)frame_index);
    fflush(stderr);
    gpu_trace_record_event("FRAME_COMMIT", "BEGIN frame=%lu", (unsigned long)frame_index);
}

void gpu_trace_frame_commit_end(uint64_t frame_index) {
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "[FRAME-TRACE] COMMIT-END frame=%lu\n", (unsigned long)frame_index);
    fflush(stderr);
    gpu_trace_record_event("FRAME_COMMIT", "END frame=%lu", (unsigned long)frame_index);
}

void gpu_trace_apply_uniforms(uint64_t pass_serial, uint64_t frame_index, int ub_slot, size_t byte_size) {
    size_t cumulative = g_frame_cumulative_uniform_bytes.fetch_add(byte_size, std::memory_order_relaxed) + byte_size;
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "[UNIFORM-TRACE] frame=%lu pass_serial=%lu slot=%d size=%zu cum_frame_bytes=%zu\n",
            (unsigned long)frame_index, (unsigned long)pass_serial, ub_slot, byte_size, cumulative);
    fflush(stderr);
    gpu_trace_record_event("UNIFORM_APPLY", "frame=%lu pass=%lu slot=%d size=%zu cum=%zu", (unsigned long)frame_index,
                           (unsigned long)pass_serial, ub_slot, byte_size, cumulative);
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
    fprintf(stderr, "] smp=[");
    for (int i = 0; i < SG_MAX_SAMPLER_BINDSLOTS; ++i) {
        if (bind->samplers[i].id != SG_INVALID_ID) {
            fprintf(stderr, "%d:s=%u ", i, bind->samplers[i].id);
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
        if (g_ring[idx].length > 0) {
            (void)write(STDERR_FILENO, g_ring[idx].text, g_ring[idx].length);
        }
    }
    fprintf(stderr, "======================================================\n\n");
    fflush(stderr);
}

// Async-signal-safe: pre-formatted chunks and write(2) only, no allocation or locks.
void gpu_trace_dump_history_signal_safe() {
    const char header[] = "\n=== [CRASH GPU-TRACE RECENT EVENT HISTORY] ===\n";
    (void)write(STDERR_FILENO, header, sizeof(header) - 1);
    size_t head = g_ring_head.load(std::memory_order_relaxed);
    size_t count = head < kRingCapacity ? head : kRingCapacity;
    size_t start = head < kRingCapacity ? 0 : (head % kRingCapacity);
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (start + i) % kRingCapacity;
        if (g_ring[idx].length > 0) {
            (void)write(STDERR_FILENO, g_ring[idx].text, g_ring[idx].length);
        }
    }
    const char footer[] = "===============================================\n\n";
    (void)write(STDERR_FILENO, footer, sizeof(footer) - 1);
}
