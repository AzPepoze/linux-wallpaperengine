#include <stdio.h>

#include <mutex>
#include <string>

#include "gpu_trace.h"
#include "gpu_trace_internal.h"

std::unordered_map<uint32_t, DebugImageRecord> g_registry_images;
std::unordered_map<uint32_t, DebugViewRecord> g_registry_views;
std::unordered_map<uint32_t, DebugBufferRecord> g_registry_buffers;
std::mutex g_registry_mutex;

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
