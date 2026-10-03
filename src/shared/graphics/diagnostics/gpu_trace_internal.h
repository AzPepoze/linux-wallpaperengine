#ifndef GPU_TRACE_INTERNAL_H
#define GPU_TRACE_INTERNAL_H

#include <stdint.h>

#include <mutex>
#include <string>
#include <unordered_map>

#include "sokol_gfx.h"

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

extern std::unordered_map<uint32_t, DebugImageRecord> g_registry_images;
extern std::unordered_map<uint32_t, DebugViewRecord> g_registry_views;
extern std::unordered_map<uint32_t, DebugBufferRecord> g_registry_buffers;
extern std::mutex g_registry_mutex;

#endif  // GPU_TRACE_INTERNAL_H
