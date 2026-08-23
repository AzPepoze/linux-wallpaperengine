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

// ============================================================================
// 1. Pass Serial Stack (Solves Sticky Global Serial)
// ============================================================================

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

// ============================================================================
// 2. Async-Signal-Safe Event Ring (Pre-formatted in Normal Execution)
// ============================================================================

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

// ============================================================================
// 3. Debug Resource Lifetime Registry
// ============================================================================

struct DebugImageRecord {
    uint32_t id = SG_INVALID_ID;
    bool alive = false;
    std::string logical_name;
    std::string owner_kind;
    uint64_t generation = 0;
    uint64_t create_pass_serial = 0;
    uint64_t destroy_pass_serial = 0;
    int width = 0;
    int height = 0;
    std::string destroy_reason;
};

struct DebugViewRecord {
    uint32_t id = SG_INVALID_ID;
    uint32_t image_id = SG_INVALID_ID;
    bool alive = false;
    std::string logical_name;
    std::string owner_kind;
    uint64_t generation = 0;
    uint64_t create_pass_serial = 0;
    uint64_t destroy_pass_serial = 0;
    std::string destroy_reason;
};

struct DebugBufferRecord {
    uint32_t id = SG_INVALID_ID;
    bool alive = false;
    size_t size = 0;
    std::string role;
    std::string owner_kind;
    std::string logical_name;
    uint64_t create_pass_serial = 0;
    uint64_t destroy_pass_serial = 0;
    std::string destroy_reason;
};

static std::unordered_map<uint32_t, DebugImageRecord> g_registry_images;
static std::unordered_map<uint32_t, DebugViewRecord> g_registry_views;
static std::unordered_map<uint32_t, DebugBufferRecord> g_registry_buffers;
static std::mutex g_registry_mutex;

void gpu_trace_register_image(uint32_t id, const char* owner_kind, const char* name, int width, int height,
                              uint64_t generation) {
    if (id == SG_INVALID_ID) return;
    uint64_t cur_serial = gpu_trace_get_current_pass_serial();
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    DebugImageRecord& rec = g_registry_images[id];
    rec.id = id;
    rec.alive = true;
    rec.owner_kind = owner_kind ? owner_kind : "generic";
    rec.logical_name = name ? name : "";
    rec.width = width;
    rec.height = height;
    rec.generation = generation;
    rec.create_pass_serial = cur_serial;
    rec.destroy_pass_serial = 0;
    rec.destroy_reason.clear();

    fprintf(stderr,
            "[GPU-RESOURCE] CREATE type=image id=%u owner=\"%s\" name=\"%s\" size=%dx%d gen=%lu pass_serial=%lu\n", id,
            rec.owner_kind.c_str(), rec.logical_name.c_str(), width, height, (unsigned long)generation,
            (unsigned long)cur_serial);
    fflush(stderr);
    gpu_trace_record_event("RES_CREATE", "img=%u owner=%s name=%s %dx%d gen=%lu", id, rec.owner_kind.c_str(),
                           rec.logical_name.c_str(), width, height, (unsigned long)generation);
}

void gpu_trace_unregister_image(uint32_t id, const char* reason) {
    if (id == SG_INVALID_ID) return;
    uint64_t cur_serial = gpu_trace_get_current_pass_serial();
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto it = g_registry_images.find(id);
    if (it != g_registry_images.end()) {
        it->second.alive = false;
        it->second.destroy_pass_serial = cur_serial;
        it->second.destroy_reason = reason ? reason : "unknown";
        fprintf(stderr,
                "[GPU-RESOURCE] DESTROY type=image id=%u owner=\"%s\" name=\"%s\" gen=%lu reason=%s pass_serial=%lu\n",
                id, it->second.owner_kind.c_str(), it->second.logical_name.c_str(),
                (unsigned long)it->second.generation, it->second.destroy_reason.c_str(), (unsigned long)cur_serial);
        fflush(stderr);
        gpu_trace_record_event("RES_DESTROY", "img=%u owner=%s reason=%s", id, it->second.owner_kind.c_str(),
                               it->second.destroy_reason.c_str());
    }
}

void gpu_trace_register_view(uint32_t id, uint32_t image_id, const char* owner_kind, const char* name,
                             uint64_t generation) {
    if (id == SG_INVALID_ID) return;
    uint64_t cur_serial = gpu_trace_get_current_pass_serial();
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    DebugViewRecord& rec = g_registry_views[id];
    rec.id = id;
    rec.image_id = image_id;
    rec.alive = true;
    rec.owner_kind = owner_kind ? owner_kind : "generic";
    rec.logical_name = name ? name : "";
    rec.generation = generation;
    rec.create_pass_serial = cur_serial;
    rec.destroy_pass_serial = 0;
    rec.destroy_reason.clear();

    fprintf(stderr, "[GPU-RESOURCE] CREATE type=view id=%u img=%u owner=\"%s\" name=\"%s\" gen=%lu pass_serial=%lu\n",
            id, image_id, rec.owner_kind.c_str(), rec.logical_name.c_str(), (unsigned long)generation,
            (unsigned long)cur_serial);
    fflush(stderr);
    gpu_trace_record_event("RES_CREATE", "view=%u img=%u owner=%s name=%s gen=%lu", id, image_id,
                           rec.owner_kind.c_str(), rec.logical_name.c_str(), (unsigned long)generation);
}

void gpu_trace_unregister_view(uint32_t id, const char* reason) {
    if (id == SG_INVALID_ID) return;
    uint64_t cur_serial = gpu_trace_get_current_pass_serial();
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto it = g_registry_views.find(id);
    if (it != g_registry_views.end()) {
        it->second.alive = false;
        it->second.destroy_pass_serial = cur_serial;
        it->second.destroy_reason = reason ? reason : "unknown";
        fprintf(stderr,
                "[GPU-RESOURCE] DESTROY type=view id=%u img=%u owner=\"%s\" name=\"%s\" gen=%lu reason=%s "
                "pass_serial=%lu\n",
                id, it->second.image_id, it->second.owner_kind.c_str(), it->second.logical_name.c_str(),
                (unsigned long)it->second.generation, it->second.destroy_reason.c_str(), (unsigned long)cur_serial);
        fflush(stderr);
        gpu_trace_record_event("RES_DESTROY", "view=%u img=%u reason=%s", id, it->second.image_id,
                               it->second.destroy_reason.c_str());
    }
}

void gpu_trace_register_buffer(uint32_t id, size_t size, const char* role, const char* owner_kind, const char* name) {
    if (id == SG_INVALID_ID) return;
    uint64_t cur_serial = gpu_trace_get_current_pass_serial();
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    DebugBufferRecord& rec = g_registry_buffers[id];
    rec.id = id;
    rec.alive = true;
    rec.size = size;
    rec.role = role ? role : "generic";
    rec.owner_kind = owner_kind ? owner_kind : "generic";
    rec.logical_name = name ? name : "";
    rec.create_pass_serial = cur_serial;
    rec.destroy_pass_serial = 0;
    rec.destroy_reason.clear();

    fprintf(stderr,
            "[GPU-RESOURCE] CREATE type=buffer id=%u size=%zu role=%s owner=\"%s\" name=\"%s\" pass_serial=%lu\n", id,
            size, rec.role.c_str(), rec.owner_kind.c_str(), rec.logical_name.c_str(), (unsigned long)cur_serial);
    fflush(stderr);
    gpu_trace_record_event("RES_CREATE", "buf=%u size=%zu role=%s owner=%s", id, size, rec.role.c_str(),
                           rec.owner_kind.c_str());
}

void gpu_trace_unregister_buffer(uint32_t id, const char* reason) {
    if (id == SG_INVALID_ID) return;
    uint64_t cur_serial = gpu_trace_get_current_pass_serial();
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto it = g_registry_buffers.find(id);
    if (it != g_registry_buffers.end()) {
        it->second.alive = false;
        it->second.destroy_pass_serial = cur_serial;
        it->second.destroy_reason = reason ? reason : "unknown";
        fprintf(stderr,
                "[GPU-RESOURCE] DESTROY type=buffer id=%u role=%s owner=\"%s\" name=\"%s\" reason=%s pass_serial=%lu\n",
                id, it->second.role.c_str(), it->second.owner_kind.c_str(), it->second.logical_name.c_str(),
                it->second.destroy_reason.c_str(), (unsigned long)cur_serial);
        fflush(stderr);
        gpu_trace_record_event("RES_DESTROY", "buf=%u reason=%s", id, it->second.destroy_reason.c_str());
    }
}

// ============================================================================
// 4. Render Target Lifecycle Traces (Integrated with Registry)
// ============================================================================

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

// ============================================================================
// 5. Pass Begin / End & Commit Traces
// ============================================================================

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

// ============================================================================
// 6. Final Binding Traces & Validation
// ============================================================================

static const char* sokol_state_str(sg_resource_state st) {
    switch (st) {
        case SG_RESOURCESTATE_INITIAL:
            return "INITIAL";
        case SG_RESOURCESTATE_ALLOC:
            return "ALLOC";
        case SG_RESOURCESTATE_UNSEALED:
            return "UNSEALED";
        case SG_RESOURCESTATE_VALID:
            return "VALID";
        case SG_RESOURCESTATE_FAILED:
            return "FAILED";
        case SG_RESOURCESTATE_INVALID:
            return "INVALID";
        default:
            return "UNKNOWN";
    }
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

bool gpu_trace_validate_bindings(uint64_t pass_serial, uint64_t frame_index, const sg_bindings* bind) {
    if (!bind) return true;

    std::lock_guard<std::mutex> reg_lock(g_registry_mutex);

    // Validate Views and underlying Images
    for (int slot = 0; slot < 12; ++slot) {
        uint32_t v_id = bind->views[slot].id;
        if (v_id == SG_INVALID_ID) continue;

        sg_resource_state v_state = sg_query_view_state(bind->views[slot]);
        if (v_state == SG_RESOURCESTATE_FAILED || v_state == SG_RESOURCESTATE_INVALID) {
            fprintf(
                stderr,
                "[GPU-VALIDATION] INVALID SOKOL VIEW STATE BOUND frame=%lu pass_serial=%lu slot=%d view=%u state=%s\n",
                (unsigned long)frame_index, (unsigned long)pass_serial, slot, v_id, sokol_state_str(v_state));
            fflush(stderr);
            return false;
        }

        auto it_v = g_registry_views.find(v_id);
        if (it_v != g_registry_views.end()) {
            if (!it_v->second.alive) {
                fprintf(
                    stderr,
                    "[GPU-VALIDATION] DEAD RESOURCE BOUND frame=%lu pass_serial=%lu slot=%d resource_type=view view=%u "
                    "image=%u owner=\"%s\" name=\"%s\" gen=%lu created_at_serial=%lu destroyed_at_serial=%lu "
                    "reason=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, slot, v_id, it_v->second.image_id,
                    it_v->second.owner_kind.c_str(), it_v->second.logical_name.c_str(),
                    (unsigned long)it_v->second.generation, (unsigned long)it_v->second.create_pass_serial,
                    (unsigned long)it_v->second.destroy_pass_serial, it_v->second.destroy_reason.c_str());
                fflush(stderr);
                return false;
            }
        }

        sg_image img = sg_query_view_image(bind->views[slot]);
        if (img.id != SG_INVALID_ID) {
            sg_resource_state img_state = sg_query_image_state(img);
            if (img_state == SG_RESOURCESTATE_FAILED || img_state == SG_RESOURCESTATE_INVALID) {
                fprintf(stderr,
                        "[GPU-VALIDATION] INVALID SOKOL IMAGE STATE BOUND frame=%lu pass_serial=%lu slot=%d view=%u "
                        "image=%u "
                        "state=%s\n",
                        (unsigned long)frame_index, (unsigned long)pass_serial, slot, v_id, img.id,
                        sokol_state_str(img_state));
                fflush(stderr);
                return false;
            }

            auto it_img = g_registry_images.find(img.id);
            if (it_img != g_registry_images.end()) {
                if (!it_img->second.alive) {
                    fprintf(
                        stderr,
                        "[GPU-VALIDATION] DEAD RESOURCE BOUND frame=%lu pass_serial=%lu slot=%d resource_type=image "
                        "view=%u image=%u owner=\"%s\" name=\"%s\" gen=%lu created_at_serial=%lu "
                        "destroyed_at_serial=%lu "
                        "reason=%s\n",
                        (unsigned long)frame_index, (unsigned long)pass_serial, slot, v_id, img.id,
                        it_img->second.owner_kind.c_str(), it_img->second.logical_name.c_str(),
                        (unsigned long)it_img->second.generation, (unsigned long)it_img->second.create_pass_serial,
                        (unsigned long)it_img->second.destroy_pass_serial, it_img->second.destroy_reason.c_str());
                    fflush(stderr);
                    return false;
                }
            }
        }
    }

    // Validate Vertex Buffers
    for (int vb_slot = 0; vb_slot < SG_MAX_VERTEXBUFFER_BINDSLOTS; ++vb_slot) {
        uint32_t b_id = bind->vertex_buffers[vb_slot].id;
        if (b_id == SG_INVALID_ID) continue;

        sg_resource_state b_state = sg_query_buffer_state(bind->vertex_buffers[vb_slot]);
        if (b_state == SG_RESOURCESTATE_FAILED || b_state == SG_RESOURCESTATE_INVALID) {
            fprintf(stderr,
                    "[GPU-VALIDATION] INVALID SOKOL BUFFER STATE BOUND frame=%lu pass_serial=%lu vb_slot=%d buf=%u "
                    "state=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, vb_slot, b_id, sokol_state_str(b_state));
            fflush(stderr);
            return false;
        }

        auto it_buf = g_registry_buffers.find(b_id);
        if (it_buf != g_registry_buffers.end()) {
            if (!it_buf->second.alive) {
                fprintf(
                    stderr,
                    "[GPU-VALIDATION] DEAD RESOURCE BOUND frame=%lu pass_serial=%lu vb_slot=%d resource_type=buffer "
                    "buf=%u size=%zu role=%s owner=\"%s\" name=\"%s\" created_at_serial=%lu destroyed_at_serial=%lu "
                    "reason=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, vb_slot, b_id, it_buf->second.size,
                    it_buf->second.role.c_str(), it_buf->second.owner_kind.c_str(), it_buf->second.logical_name.c_str(),
                    (unsigned long)it_buf->second.create_pass_serial, (unsigned long)it_buf->second.destroy_pass_serial,
                    it_buf->second.destroy_reason.c_str());
                fflush(stderr);
                return false;
            }
        }
    }

    // Validate Index Buffer
    uint32_t ib_id = bind->index_buffer.id;
    if (ib_id != SG_INVALID_ID) {
        sg_resource_state ib_state = sg_query_buffer_state(bind->index_buffer);
        if (ib_state == SG_RESOURCESTATE_FAILED || ib_state == SG_RESOURCESTATE_INVALID) {
            fprintf(stderr,
                    "[GPU-VALIDATION] INVALID SOKOL BUFFER STATE BOUND frame=%lu pass_serial=%lu ib=b=%u state=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, ib_id, sokol_state_str(ib_state));
            fflush(stderr);
            return false;
        }

        auto it_ib = g_registry_buffers.find(ib_id);
        if (it_ib != g_registry_buffers.end()) {
            if (!it_ib->second.alive) {
                fprintf(
                    stderr,
                    "[GPU-VALIDATION] DEAD RESOURCE BOUND frame=%lu pass_serial=%lu resource_type=index_buffer buf=%u "
                    "size=%zu role=%s owner=\"%s\" name=\"%s\" created_at_serial=%lu destroyed_at_serial=%lu "
                    "reason=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, ib_id, it_ib->second.size,
                    it_ib->second.role.c_str(), it_ib->second.owner_kind.c_str(), it_ib->second.logical_name.c_str(),
                    (unsigned long)it_ib->second.create_pass_serial, (unsigned long)it_ib->second.destroy_pass_serial,
                    it_ib->second.destroy_reason.c_str());
                fflush(stderr);
                return false;
            }
        }
    }

    return true;
}

// ============================================================================
// 7. Dump History Operations
// ============================================================================

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

// Strictly async-signal-safe: uses pre-formatted byte chunks and write(2) only.
// No malloc, free, snprintf, printf, mutex, or dynamic allocation.
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
