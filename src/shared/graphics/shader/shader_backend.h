#ifndef SHADER_BACKEND_H
#define SHADER_BACKEND_H

#include <string>

#include "sokol_gfx.h"

sg_shader create_backend_shader(sg_shader_desc* desc, const std::string& vertex_source,
                                const std::string& fragment_source, const char* label = nullptr);

// Generates and caches the SPIR-V that create_backend_shader would use without creating any GPU object, so it is safe
// to call from a worker thread. The later create_backend_shader call then hits the cache.
bool prewarm_backend_shader(sg_shader_desc* desc, const std::string& vertex_source, const std::string& fragment_source,
                            const char* label = nullptr);

void get_shader_cache_stats(uint64_t* out_hits, uint64_t* out_misses);

#endif  // SHADER_BACKEND_H
