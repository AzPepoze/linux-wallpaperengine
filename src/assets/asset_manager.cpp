#include "asset_manager.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "core/logger.h"
#include "core/utils.h"
#include "formats/wallpaper_engine/texture/tex_decoder.h"
#include "wallpaper/scene/2d/layers/image_layer.h"
#include "wallpaper/scene/2d/layers/layer.h"

namespace {

sg_pixel_format toSokolPixelFormat(wallpaper_engine::PixelFormat format) {
    switch (format) {
        case wallpaper_engine::PixelFormat::RGBA8:
            return SG_PIXELFORMAT_RGBA8;
        case wallpaper_engine::PixelFormat::RG8:
            return SG_PIXELFORMAT_RG8;
        case wallpaper_engine::PixelFormat::BC1:
            return SG_PIXELFORMAT_BC1_RGBA;
        case wallpaper_engine::PixelFormat::BC2:
            return SG_PIXELFORMAT_BC2_RGBA;
        case wallpaper_engine::PixelFormat::BC3:
            return SG_PIXELFORMAT_BC3_RGBA;
        case wallpaper_engine::PixelFormat::R8:
            return SG_PIXELFORMAT_R8;
        default:
            return SG_PIXELFORMAT_NONE;
    }
}

}  // namespace

void AssetManager::init(const char* ep, const char* wp) {
    engine_path = ep;
    wallpaper_path = wp;
}

AssetManager::~AssetManager() {
    clearVideoTextures();
}

void AssetManager::clearVideoTextures() {
    video_textures.clear();
}

const AssetManager::ActiveVideoTexture* AssetManager::findVideoTexture(sg_image img) const {
    if (img.id == SG_INVALID_ID) return nullptr;
    for (const auto& v : video_textures) {
        if (v.image.id == img.id) return &v;
    }
    return nullptr;
}

const AssetManager::ActiveVideoTexture* AssetManager::findVideoTexture(const std::string& path) const {
    if (path.empty()) return nullptr;
    for (const auto& v : video_textures) {
        if (v.path == path || (!path.empty() && v.path.find(path) != std::string::npos)) return &v;
    }
    return nullptr;
}

void AssetManager::updateVideoTextures(float elapsed_seconds, const std::vector<Layer*>& active_layers) {
    for (ActiveVideoTexture& video : video_textures) {
        if (!active_layers.empty()) {
            bool is_used_by_visible_layer = false;
            for (const auto* layer : active_layers) {
                if (!layer || !layer->visible) continue;
                if (const auto* il = dynamic_cast<const ImageLayer*>(layer)) {
                    if (il->img.id == video.image.id || (!video.path.empty() && il->path == video.path)) {
                        is_used_by_visible_layer = true;
                        break;
                    }
                }
            }
            if (!is_used_by_visible_layer) continue;
        }

        video.elapsed_seconds += elapsed_seconds;
        const float frame_dur = video.decoder->frameDuration();
        if (video.elapsed_seconds < frame_dur) continue;

        while (video.elapsed_seconds >= frame_dur * 2.0f) {
            video.elapsed_seconds -= frame_dur;
        }
        video.elapsed_seconds -= frame_dur;

        std::vector<uint8_t> pixels;
        if (video.decoder->decodeNextFrame(pixels) && !pixels.empty()) {
            sg_image_data data = {};
            data.mip_levels[0] = {pixels.data(), pixels.size()};
            sg_update_image(video.image, &data);
        }
    }
}

bool AssetManager::resolvePath(const char* rel_path, char* out_abs_path, int max_len) const {
    if (!rel_path || !out_abs_path || max_len <= 0) return false;
    if (rel_path[0] == '/' && access(rel_path, F_OK) == 0) {
        snprintf(out_abs_path, max_len, "%s", rel_path);
        return true;
    }
    snprintf(out_abs_path, max_len, "%s/%s", wallpaper_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    snprintf(out_abs_path, max_len, "%s/assets/%s", engine_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    snprintf(out_abs_path, max_len, "%s/assets/materials/%s", engine_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    snprintf(out_abs_path, max_len, "%s/materials/%s", wallpaper_path.c_str(), rel_path);
    if (access(out_abs_path, F_OK) == 0) return true;

    const char* filename = strrchr(rel_path, '/');
    if (filename) {
        filename++;
        if (strstr(rel_path, "materials/presets/") == rel_path) {
            char preset_name[64];
            const char* start = rel_path + 18;
            const char* end = strstr(start, ".json");
            if (end) {
                size_t len = end - start;
                if (len > 4) len -= 4;
                strncpy(preset_name, start, len);
                preset_name[len] = '\0';
                snprintf(out_abs_path, max_len, "%s/assets/presets/%s/%s", engine_path.c_str(), preset_name, rel_path);
                if (access(out_abs_path, F_OK) == 0) return true;
            }
        }
    }

    snprintf(out_abs_path, max_len, "%s/%s", engine_path.c_str(), rel_path);
    return access(out_abs_path, F_OK) == 0;
}

GfxImage AssetManager::resolveTexture(const char* name, std::string* out_path, int image_index) const {
    if (!name || name[0] == '\0') return {};
    if (strncmp(name, "_rt_", 4) == 0 || strstr(name, "/_rt_") != nullptr) return {};

    char abs_path[1024];
    char name_with_ext[256] = {};
    if (!strstr(name, "."))
        snprintf(name_with_ext, sizeof(name_with_ext), "%s.tex", name);
    else
        strncpy(name_with_ext, name, sizeof(name_with_ext) - 1);

    if (resolvePath(name_with_ext, abs_path, sizeof(abs_path))) {
        if (out_path) *out_path = abs_path;
        const char* ext = strrchr(abs_path, '.');
        const bool is_video =
            ext && (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".webm") == 0 || strcasecmp(ext, ".mkv") == 0 ||
                    strcasecmp(ext, ".avi") == 0 || strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".wmv") == 0);
        if (!is_video) {
            wallpaper_engine::DecodedImage image = wallpaper_engine::decodeTexture(abs_path, image_index);
            if (image.valid()) {
                const sg_pixel_format pixel_format = toSokolPixelFormat(image.format);
                if (pixel_format != SG_PIXELFORMAT_NONE) {
                    sg_image_desc desc = {};
                    desc.width = (int)image.width;
                    desc.height = (int)image.height;
                    desc.pixel_format = pixel_format;
                    desc.data.mip_levels[0] = {image.pixels.data(), image.pixels.size()};
                    return sg_make_image(&desc);
                }
            }
        }

        if (image_index == 0) {
            for (const ActiveVideoTexture& video : video_textures) {
                if (video.path == abs_path) return GfxImage(video.image);
            }
            std::unique_ptr<wallpaper_engine::VideoTexture> video = wallpaper_engine::VideoTexture::open(abs_path);
            std::vector<uint8_t> pixels;
            if (video && video->decodeNextFrame(pixels)) {
                sg_image_desc desc = {};
                desc.width = (int)video->width();
                desc.height = (int)video->height();
                desc.pixel_format = SG_PIXELFORMAT_RGBA8;
                desc.usage.stream_update = true;
                desc.data.mip_levels[0] = {pixels.data(), pixels.size()};
                const sg_image gpu_image = sg_make_image(&desc);
                if (gpu_image.id != SG_INVALID_ID) {
                    video_textures.push_back({abs_path, gpu_image, std::move(video)});
                    return GfxImage(gpu_image);
                }
            }
        }
    }

    if (image_index == 0)
        LOG_W("Failed to resolve texture: %s", name);
    else
        LOG_D("Optional texture not found or index not present: %s (index %d)", name, image_index);
    return {};
}

GfxImage AssetManager::resolveMaterialTexture(const char* mat_rel_path, std::string* out_path) const {
    char abs_path[1024];
    if (!resolvePath(mat_rel_path, abs_path, sizeof(abs_path))) return {};

    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return {};

    cJSON* mat_json = cJSON_Parse(json_str);
    free(json_str);
    if (!mat_json) return {};

    GfxImage img;

    cJSON* passes = cJSON_GetObjectItemCaseSensitive(mat_json, "passes");
    if (cJSON_IsArray(passes)) {
        cJSON* pass = cJSON_GetArrayItem(passes, 0);
        cJSON* textures = cJSON_GetObjectItemCaseSensitive(pass, "textures");
        if (cJSON_IsArray(textures)) {
            cJSON* tex_node = cJSON_GetArrayItem(textures, 0);
            if (cJSON_IsString(tex_node) && tex_node->valuestring && tex_node->valuestring[0] != '\0') {
                const std::string texture_ref = tex_node->valuestring;
                const bool material_rooted = texture_ref.rfind("materials/", 0) == 0 ||
                                             texture_ref.rfind("assets/", 0) == 0 || texture_ref[0] == '/';
                if (material_rooted) img = resolveTexture(texture_ref.c_str(), out_path);

                if (img.id == SG_INVALID_ID) {
                    const std::string material_path = abs_path;
                    const size_t slash = material_path.rfind('/');
                    if (slash != std::string::npos) {
                        const std::string relative_to_material = material_path.substr(0, slash + 1) + texture_ref;
                        img = resolveTexture(relative_to_material.c_str(), out_path);
                    }
                }
                if (img.id == SG_INVALID_ID && !material_rooted) img = resolveTexture(texture_ref.c_str(), out_path);
            }
        }
    }
    cJSON_Delete(mat_json);
    return img;
}
