#include "asset_manager.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <future>
#include <mutex>
#include <unordered_map>

#include "shared/assets/media/video_rate.h"
#include "shared/assets/shared_assets.h"
#include "shared/assets/tex_decoder.h"
#include "shared/core/logger.h"
#include "shared/core/task_pool.h"
#include "shared/core/utils.h"
#include "shared/core/vfs.h"
#include "shared/graphics/backend/gpu_zero_copy.h"
#include "wallpaper/2d/layers/layer.h"

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

AssetManager::AssetManager() : wallpaper_decode_cache_(std::make_unique<TextureDecodeCache>()) {}

AssetManager::~AssetManager() {
    clearVideoTextures();
}

void AssetManager::attachShared(SharedAssets* shared) {
    shared_ = shared;
    engine_path = shared ? shared->engine_path : "";
}

void AssetManager::initWallpaper(const char* wp) {
    wallpaper_path = wp ? wp : "";
    wallpaper_provider = std::make_unique<WallpaperAssetProvider>(wallpaper_path);
    wallpaper_decode_cache_ = std::make_unique<TextureDecodeCache>();
}

void AssetManager::init(const char* ep, const char* wp) {
    // Temporary shim until every caller supplies a process-wide SharedAssets:
    // owns a private one so behavior is unchanged.
    clearVideoTextures();
    if (!owned_shared_) owned_shared_ = std::make_unique<SharedAssets>();
    owned_shared_->engine_path = ep ? ep : "";
    owned_shared_->engine_provider = std::make_unique<EngineAssetProvider>(owned_shared_->engine_path);
    owned_shared_->internal_provider = std::make_unique<InternalAssetProvider>();
    owned_shared_->decode_cache = std::make_unique<TextureDecodeCache>();
    attachShared(owned_shared_.get());
    initWallpaper(wp);
}

void AssetManager::prefetchPackageTextures() const {
    constexpr size_t kMaxTextureBytes = 32u << 20;
    constexpr size_t kMaxTotalBytes = 192u << 20;
    size_t total_bytes = 0;

    vfs::forEachFile([&](const char* name) {
        const size_t length = strlen(name);
        if (length < 4 || strcasecmp(name + length - 4, ".tex") != 0) return false;

        const std::string path = std::string(vfs::kRoot) + "/" + name;
        const uint8_t* data = nullptr;
        size_t size = 0;
        if (!vfs::find(path.c_str(), data, size) || size > kMaxTextureBytes) return false;
        if (total_bytes + size > kMaxTotalBytes) return true;
        total_bytes += size;

        wallpaper_decode_cache_->prefetch(path.c_str(), 0);
        return false;
    });
}

void AssetManager::releaseDecodedTextures() const {
    wallpaper_decode_cache_->release();
}

const TextureDecodeCache& AssetManager::decodeCacheFor(const char* abs_path) const {
    // Engine install textures are shared across instances; wallpaper (and
    // internal) textures live in this instance's cache and are released on switch.
    if (shared_ && shared_->decode_cache && !shared_->engine_path.empty() &&
        strncmp(abs_path, shared_->engine_path.c_str(), shared_->engine_path.size()) == 0) {
        return *shared_->decode_cache;
    }
    return *wallpaper_decode_cache_;
}

std::shared_ptr<const wallpaper_engine::DecodedImage> AssetManager::decodeShared(const char* abs_path,
                                                                                 int image_index) const {
    return decodeCacheFor(abs_path).decode(abs_path, image_index);
}

void AssetManager::setVideoPlayback(float rate, float volume) {
    video_rate_ = clampPlaybackRate(rate);
    video_volume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AssetManager::setVideoPaused(bool paused) {
    if (video_paused_ == paused) return;
    video_paused_ = paused;
    for (ActiveVideoTexture& video : video_textures) {
        if (paused)
            video.decoder->pause();
        else
            video.decoder->resume();
        AudioEngine::instance().setStreamPaused(video.audio_stream, paused);
    }
}

// The image starts black so nothing uninitialised shows before the first decoded frame arrives.
sg_image AssetManager::makeVideoImage(const char* path, std::unique_ptr<wallpaper_engine::VideoTexture> video) const {
    sg_image_desc desc = {};
    desc.width = (int)video->width();
    desc.height = (int)video->height();
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.usage.color_attachment = true;
    desc.usage.stream_update = true;

    std::vector<uint8_t> pixels;
    bool has_frame = false;
    if (!video->isZeroCopy()) {
        has_frame = video->decodeNextFrame(pixels) && !pixels.empty();
        if (!has_frame) pixels.assign((size_t)desc.width * desc.height * 4, 0);
        desc.data.mip_levels[0] = {pixels.data(), pixels.size()};
    }
    const sg_image image = sg_make_image(&desc);
    if (image.id == SG_INVALID_ID) return image;

    if (video->isZeroCopy()) {
        ImportedVideoSurface* surface = nullptr;
        AVFrame* av_frame = nullptr;
        if (video->decodeNextFrameZeroCopy(surface, av_frame) && surface) {
            gpu_blit_zero_copy_surface(*surface, image, desc.width, desc.height);
        } else {
            pixels.assign((size_t)desc.width * desc.height * 4, 0);
            sg_image_data black = {};
            black.mip_levels[0] = {pixels.data(), pixels.size()};
            sg_update_image(image, &black);
        }
    }
    addVideoTexture(path, image, std::move(video));
    return image;
}

void AssetManager::addVideoTexture(const char* path, sg_image image,
                                   std::unique_ptr<wallpaper_engine::VideoTexture> video) const {
    video_textures.push_back({});
    ActiveVideoTexture& entry = video_textures.back();
    entry.path = path;
    entry.image = image;
    entry.decoder = std::move(video);
    if (!AudioEngine::instance().isAvailable()) return;
    entry.audio = VideoAudioStream::open(path);
    if (entry.audio) entry.audio->setRate(video_rate_);
}

void AssetManager::clearVideoTextures() {
    for (auto& video : video_textures) {
        if (video.audio) video.audio->stopFeeder();
        if (video.audio_stream != AudioEngine::kInvalidStream) {
            AudioEngine::instance().destroyStream(video.audio_stream);
        }
    }
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
    if (video_paused_) return;
    for (ActiveVideoTexture& video : video_textures) {
        if (!active_layers.empty()) {
            bool is_used_by_visible_layer = false;
            for (const auto* layer : active_layers) {
                if (!layer || !layer->visible) continue;
                if (layer->usesTexture(video.image) || (!video.path.empty() && layer->usesTexturePath(video.path))) {
                    is_used_by_visible_layer = true;
                    break;
                }
            }
            if (!is_used_by_visible_layer) {
                if (video.audio && video.audio_stream != AudioEngine::kInvalidStream)
                    video.audio->setFeeding(video.audio_stream, false);
                continue;
            }
        }

        if (video.audio && video.audio->hasAudio()) {
            if (video.audio_stream == AudioEngine::kInvalidStream && AudioEngine::instance().isAvailable()) {
                video.audio_stream = AudioEngine::instance().createStream(48000, 2, audio_group_);
                if (video.audio_stream != AudioEngine::kInvalidStream)
                    AudioEngine::instance().setStreamVolume(video.audio_stream, video_volume_);
            }
            if (video.audio_stream != AudioEngine::kInvalidStream) {
                const uint32_t loops = video.decoder->loopCount();
                if (loops != video.audio_loop_seen) {
                    video.audio_loop_seen = loops;
                    video.audio->restart(video.audio_stream);
                }
                // Decoding runs on the audio feeder thread so render stalls cannot starve playback.
                video.audio->setFeeding(video.audio_stream, true);
                video.audio->startFeeder(video.audio_stream, 24000);
            }
        }

        video.elapsed_seconds += elapsed_seconds * video_rate_;
        const float frame_dur = video.decoder->frameDuration();
        if (video.elapsed_seconds < frame_dur) continue;

        while (video.elapsed_seconds >= frame_dur * 2.0f) {
            video.elapsed_seconds -= frame_dur;
        }
        video.elapsed_seconds -= frame_dur;

        if (video.decoder->isZeroCopy()) {
            ImportedVideoSurface* surface = nullptr;
            AVFrame* av_frame = nullptr;
            if (video.decoder->decodeNextFrameZeroCopy(surface, av_frame) && surface) {
                gpu_blit_zero_copy_surface(*surface, video.image, (int)video.decoder->width(),
                                           (int)video.decoder->height());
            }
        } else {
            std::vector<uint8_t> pixels;
            if (video.decoder->decodeNextFrame(pixels) && !pixels.empty()) {
                sg_image_data data = {};
                data.mip_levels[0] = {pixels.data(), pixels.size()};
                sg_update_image(video.image, &data);
            }
        }
    }
}

bool AssetManager::resolvePath(const char* rel_path, char* out_abs_path, int max_len) const {
    if (!rel_path || !out_abs_path || max_len <= 0) return false;

    if (shared_ && shared_->internal_provider &&
        shared_->internal_provider->resolvePath(rel_path, out_abs_path, max_len)) {
        return true;
    }

    if (wallpaper_provider && wallpaper_provider->resolvePath(rel_path, out_abs_path, max_len)) {
        return true;
    }

    if (shared_ && shared_->engine_provider && shared_->engine_provider->resolvePath(rel_path, out_abs_path, max_len)) {
        return true;
    }

    return false;
}

GfxImage AssetManager::resolveTexture(const char* name, std::string* out_path, int image_index) const {
    return resolveTextureInternal(name, out_path, image_index, true);
}

GfxImage AssetManager::resolveTextureInternal(const char* name, std::string* out_path, int image_index,
                                              bool warn_on_failure) const {
    if (!name || name[0] == '\0') return {};
    if (strncmp(name, "_rt_", 4) == 0 || strstr(name, "/_rt_") != nullptr) return {};

    char abs_path[1024];
    char name_with_ext[256] = {};
    if (!strstr(name, "."))
        snprintf(name_with_ext, sizeof(name_with_ext), "%s.tex", name);
    else
        strncpy(name_with_ext, name, sizeof(name_with_ext) - 1);

    const bool resolved_file = vfs::exists(name);
    if (resolved_file) snprintf(abs_path, sizeof(abs_path), "%s", name);
    if (resolved_file || resolvePath(name_with_ext, abs_path, sizeof(abs_path))) {
        if (out_path) *out_path = abs_path;
        const char* ext = strrchr(abs_path, '.');
        const bool is_video =
            ext && (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".webm") == 0 || strcasecmp(ext, ".mkv") == 0 ||
                    strcasecmp(ext, ".avi") == 0 || strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".wmv") == 0);
        if (!is_video) {
            const std::shared_ptr<const wallpaper_engine::DecodedImage> decoded = decodeShared(abs_path, image_index);
            const wallpaper_engine::DecodedImage& image = *decoded;
            if (!image.is_video && image.valid()) {
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
            auto video = wallpaper_engine::VideoTexture::open(abs_path);
            if (video) {
                const sg_image image = makeVideoImage(abs_path, std::move(video));
                if (image.id != SG_INVALID_ID) return GfxImage(image);
            }
        }
    }

    if (!warn_on_failure)
        LOG_D("Texture candidate not resolved: %s", name);
    else if (image_index == 0)
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
                if (material_rooted) img = resolveTextureInternal(texture_ref.c_str(), out_path, 0, false);

                if (img.id == SG_INVALID_ID) {
                    const std::string material_path = abs_path;
                    const size_t slash = material_path.rfind('/');
                    if (slash != std::string::npos) {
                        const std::string relative_to_material = material_path.substr(0, slash + 1) + texture_ref;
                        img = resolveTextureInternal(relative_to_material.c_str(), out_path, 0, false);
                    }
                }
                if (img.id == SG_INVALID_ID && !material_rooted)
                    img = resolveTextureInternal(texture_ref.c_str(), out_path, 0, false);
                if (img.id == SG_INVALID_ID) LOG_W("Failed to resolve texture: %s", texture_ref.c_str());
            }
        }
    }
    cJSON_Delete(mat_json);
    return img;
}
