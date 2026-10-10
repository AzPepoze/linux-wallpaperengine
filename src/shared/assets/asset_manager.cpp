#include "asset_manager.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <unordered_map>

#include "shared/assets/media/video_rate.h"
#include "shared/assets/shared_assets.h"
#include "shared/assets/tex_decoder.h"
#include "shared/core/load_trace.h"
#include "shared/core/logger.h"
#include "shared/core/task_pool.h"
#include "shared/core/utils.h"
#include "shared/core/vfs.h"
#include "shared/graphics/backend/gpu_zero_copy.h"
#include "shared/media/media_thumbnail_texture.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/parser/scene_document.h"
#include "wallpaper/user_properties.h"

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

// The image the user picked for a usertextures entry that names a scenetexture property.
const std::string* pickedTexture(const cJSON* key, const UserProperties* properties) {
    if (!properties || !cJSON_IsString(key) || !key->valuestring) return nullptr;
    return properties->texturePath(key->valuestring);
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

void AssetManager::prepareWallpaper(const char* root) {
    initWallpaper(root);
    prefetchPackageTextures();
    wallpaper_decode_cache_->waitForPrefetches();
}

bool AssetManager::prepareVideo(const char* path) {
    if (!path || !path[0]) return false;
    char resolved_path[1024];
    std::string key = resolvePath(path, resolved_path, sizeof(resolved_path)) ? resolved_path : path;
    if (!vfs::exists(key.c_str()) && key.find('.') == std::string::npos) {
        const std::string with_ext = key + ".tex";
        if (resolvePath(with_ext.c_str(), resolved_path, sizeof(resolved_path))) key = resolved_path;
    }
    PreparedVideo prepared;
    prepared.decoder = wallpaper_engine::VideoTexture::openPrepared(key.c_str());
    if (prepared.decoder) prepared.decoder->decodeNextFrame(prepared.first_frame);
    if (!prepared.decoder || prepared.first_frame.empty()) {
        failed_prepared_videos_.insert(key);
        return false;
    }
    prepared.audio = VideoAudioStream::open(key.c_str());
    prepared_video_data_[key] = std::move(prepared);
    return true;
}

bool AssetManager::textureReady(const char* path, int image_index) const {
    if (!path || !path[0]) return false;
    char abs_path[1024];
    const char* suffix = strrchr(path, '.');
    if (suffix && strcasecmp(suffix, ".json") == 0) return true;
    const bool has_extension = suffix != nullptr;
    char with_ext[1024];
    const char* candidate = path;
    if (!has_extension) {
        snprintf(with_ext, sizeof(with_ext), "%s.tex", path);
        candidate = with_ext;
    }
    if (vfs::exists(path))
        snprintf(abs_path, sizeof(abs_path), "%s", path);
    else if (!resolvePath(candidate, abs_path, sizeof(abs_path)))
        return true;
    return decodeCacheFor(abs_path).ready(abs_path, image_index);
}

bool AssetManager::prepareSceneAssets(const wallpaper_engine::SceneDocument& document) {
    std::unordered_set<std::string> visited_documents;
    std::unordered_set<std::string> textures;
    std::unordered_set<std::string> videos;
    std::vector<const TextureDecodeCache*> touched_caches;

    const auto is_file = [](const std::string& path) {
        if (vfs::isVirtual(path.c_str())) return vfs::exists(path.c_str());
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    };
    const auto resolve = [&](const std::string& reference, const std::string& owner) {
        char absolute[1024];
        if (is_file(reference)) return reference;
        if (resolvePath(reference.c_str(), absolute, sizeof(absolute)) && is_file(absolute))
            return std::string(absolute);
        if (reference.find_last_of('.') == std::string::npos &&
            resolvePath((reference + ".tex").c_str(), absolute, sizeof(absolute)))
            return std::string(absolute);
        if (!owner.empty()) {
            const size_t slash = owner.find_last_of('/');
            if (slash != std::string::npos) {
                const std::string relative = owner.substr(0, slash + 1) + reference;
                if (is_file(relative)) return relative;
                if (resolvePath(relative.c_str(), absolute, sizeof(absolute)) && is_file(absolute))
                    return std::string(absolute);
            }
        }
        return std::string();
    };

    std::function<void(const std::string&, const std::string&)> visit_string;
    std::function<void(cJSON*, const std::string&)> visit_json;
    visit_json = [&](cJSON* node, const std::string& owner) {
        for (cJSON* child = node ? node->child : nullptr; child; child = child->next) {
            if (cJSON_IsString(child) && child->valuestring)
                visit_string(child->valuestring, owner);
            else if (child->child)
                visit_json(child, owner);
        }
    };
    visit_string = [&](const std::string& reference, const std::string& owner) {
        const size_t dot = reference.find_last_of('.');
        const std::string extension = dot == std::string::npos ? ".tex" : reference.substr(dot);
        const bool is_json = strcasecmp(extension.c_str(), ".json") == 0;
        const bool is_video =
            strcasecmp(extension.c_str(), ".mp4") == 0 || strcasecmp(extension.c_str(), ".webm") == 0 ||
            strcasecmp(extension.c_str(), ".mkv") == 0 || strcasecmp(extension.c_str(), ".avi") == 0 ||
            strcasecmp(extension.c_str(), ".mov") == 0 || strcasecmp(extension.c_str(), ".wmv") == 0;
        const bool is_texture = dot == std::string::npos || strcasecmp(extension.c_str(), ".tex") == 0 ||
                                strcasecmp(extension.c_str(), ".png") == 0 ||
                                strcasecmp(extension.c_str(), ".jpg") == 0 ||
                                strcasecmp(extension.c_str(), ".jpeg") == 0;
        if (!is_json && !is_video && !is_texture) return;
        const std::string path = resolve(reference, owner);
        if (path.empty()) return;
        if (is_video) {
            videos.insert(path);
            return;
        }
        if (is_texture) {
            if (textures.insert(path).second) {
                textureReady(path.c_str(), 0);
                const TextureDecodeCache& cache = decodeCacheFor(path.c_str());
                touched_caches.push_back(&cache);
                if (strcasecmp(extension.c_str(), ".tex") == 0) {
                    textureReady(path.c_str(), 1);
                }
            }
            return;
        }
        if (!visited_documents.insert(path).second) return;
        char* json_text = read_file_to_string(path.c_str());
        if (!json_text) return;
        cJSON* json = cJSON_Parse(json_text);
        free(json_text);
        if (json) {
            visit_json(json, path);
            cJSON_Delete(json);
        }
    };

    for (const auto& object : document.objects) {
        if (!object.raw_json.empty()) {
            cJSON* raw = cJSON_Parse(object.raw_json.c_str());
            if (raw) {
                visit_json(raw, wallpaper_path);
                cJSON_Delete(raw);
            }
        }
        if (object.kind == wallpaper_engine::SceneObjectKind::Image && !object.image.image.empty())
            visit_string(object.image.image, wallpaper_path);
        if (object.kind == wallpaper_engine::SceneObjectKind::Particle && !object.particle.particle.empty())
            visit_string(object.particle.particle, wallpaper_path);
        for (const auto& effect : object.effects)
            if (!effect.file.empty()) visit_string(effect.file, wallpaper_path);
    }

    for (const TextureDecodeCache* cache : touched_caches) cache->waitForPrefetches();
    bool ready = true;
    for (const std::string& path : textures) {
        const std::shared_ptr<const wallpaper_engine::DecodedImage> decoded = decodeShared(path.c_str(), 0);
        if (decoded && decoded->is_video) videos.insert(path);
    }
    for (const std::string& path : videos) ready = prepareVideo(path.c_str()) && ready;
    return ready;
}

void AssetManager::init(const char* ep, const char* wp) {
    // Temporary: owns a private SharedAssets until every caller uses attachShared.
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
    const auto supported = [](const char* name) {
        const size_t length = strlen(name);
        if (length < 4) return false;
        const char* extension = name + length - 4;
        return strcasecmp(extension, ".tex") == 0 || strcasecmp(extension, ".png") == 0 ||
               strcasecmp(extension, ".jpg") == 0 || (length >= 5 && strcasecmp(name + length - 5, ".jpeg") == 0);
    };
    const auto enqueue = [&](const std::string& path) {
        wallpaper_decode_cache_->prefetch(path.c_str(), 0);
        const char* ext = strrchr(path.c_str(), '.');
        if (ext && strcasecmp(ext, ".tex") == 0) wallpaper_decode_cache_->prefetch(path.c_str(), 1);
    };
    vfs::forEachFile([&](const char* name) {
        if (!supported(name)) return false;
        const std::string path = std::string(vfs::kRoot) + "/" + name;
        enqueue(path);
        return false;
    });

    std::error_code ec;
    if (!wallpaper_path.empty() && std::filesystem::is_directory(wallpaper_path, ec)) {
        for (std::filesystem::recursive_directory_iterator it(wallpaper_path, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            const bool regular_file = it->is_regular_file(ec);
            if (ec) {
                ec.clear();
                continue;
            }
            if (!regular_file) continue;
            const std::string path = it->path().string();
            if (supported(path.c_str())) enqueue(path);
        }
    }
}

void AssetManager::releaseDecodedTextures() const {
    wallpaper_decode_cache_->releaseAsync();
}

const TextureDecodeCache& AssetManager::decodeCacheFor(const char* abs_path) const {
    // Engine textures are shared across instances; wallpaper textures are released on switch.
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

content_bounds::Rect AssetManager::textureContentBounds(const char* abs_path) const {
    if (!abs_path || abs_path[0] == '\0') return {};
    const std::string key = abs_path;
    const auto cached = content_bounds_cache_.find(key);
    if (cached != content_bounds_cache_.end()) return cached->second;
    const std::shared_ptr<const wallpaper_engine::DecodedImage> decoded = decodeShared(abs_path, 0);
    if (!decoded) return {};
    const content_bounds::Rect bounds = decoded->is_video ? content_bounds::Rect{} : content_bounds::fromImage(*decoded);
    content_bounds_cache_.emplace(key, bounds);
    return bounds;
}

bool AssetManager::textureIsOpaque(const char* abs_path) const {
    if (!abs_path || abs_path[0] == '\0') return false;
    const std::string key = abs_path;
    const auto cached = opacity_cache_.find(key);
    if (cached != opacity_cache_.end()) return cached->second;
    const std::shared_ptr<const wallpaper_engine::DecodedImage> decoded = decodeShared(abs_path, 0);
    if (!decoded) return false;
    const bool opaque = !decoded->is_video && content_bounds::isOpaque(*decoded);
    opacity_cache_.emplace(key, opaque);
    return opaque;
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
sg_image AssetManager::makeVideoImage(const char* path, std::unique_ptr<wallpaper_engine::VideoTexture> video,
                                      std::vector<uint8_t> first_frame, std::unique_ptr<VideoAudioStream> audio,
                                      bool audio_prepared) const {
    video->initializeGpu();
    sg_image_desc desc = {};
    desc.width = (int)video->width();
    desc.height = (int)video->height();
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.usage.color_attachment = true;
    desc.usage.stream_update = true;

    if (first_frame.empty()) video->decodeNextFrame(first_frame);
    if (first_frame.empty()) first_frame.assign((size_t)desc.width * desc.height * 4, 0);
    desc.data.mip_levels[0] = {first_frame.data(), first_frame.size()};
    const sg_image image = sg_make_image(&desc);
    if (image.id == SG_INVALID_ID) return image;
    addVideoTexture(path, image, std::move(video), std::move(audio), audio_prepared);
    return image;
}

void AssetManager::addVideoTexture(const char* path, sg_image image,
                                   std::unique_ptr<wallpaper_engine::VideoTexture> video,
                                   std::unique_ptr<VideoAudioStream> audio, bool audio_prepared) const {
    video_textures.push_back({});
    ActiveVideoTexture& entry = video_textures.back();
    entry.path = path;
    entry.image = image;
    entry.decoder = std::move(video);
    if (!AudioEngine::instance().isAvailable()) return;
    entry.audio = std::move(audio);
    if (!audio_prepared && !entry.audio) entry.audio = VideoAudioStream::open(path);
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
    prepared_video_data_.clear();
    failed_prepared_videos_.clear();
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

        const float step = elapsed_seconds * video_rate_ * video.rate;
        if (video.decoder->loopCount() != video.position_loop) {
            video.position_loop = video.decoder->loopCount();
            video.position = 0.0;
        }
        if (video.decoder->isPlaying()) {
            const double length = video.decoder->duration();
            video.position = length > 0.0 ? std::min(video.position + step, length) : video.position + step;
        }
        video.elapsed_seconds += step;
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
    if (strcmp(name, "$mediaThumbnail") == 0 || strcmp(name, "$mediaPreviousThumbnail") == 0) {
        if (out_path) *out_path = name;
        return GfxImage(
            wallpaper_engine::MediaThumbnailTexture::instance().create(strcmp(name, "$mediaPreviousThumbnail") == 0));
    }
    if (strncmp(name, "_rt_", 4) == 0 || strstr(name, "/_rt_") != nullptr) return {};

    char abs_path[1024];
    char name_with_ext[256] = {};
    const char* suffix = strrchr(name, '.');
    const bool has_extension =
        suffix &&
        (strcasecmp(suffix, ".tex") == 0 || strcasecmp(suffix, ".png") == 0 || strcasecmp(suffix, ".jpg") == 0 ||
         strcasecmp(suffix, ".jpeg") == 0 || strcasecmp(suffix, ".gif") == 0 || strcasecmp(suffix, ".mp4") == 0 ||
         strcasecmp(suffix, ".webm") == 0 || strcasecmp(suffix, ".mkv") == 0 || strcasecmp(suffix, ".avi") == 0 ||
         strcasecmp(suffix, ".mov") == 0 || strcasecmp(suffix, ".wmv") == 0);
    if (!has_extension)
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
            const auto trace_decode_start = std::chrono::steady_clock::now();
            const std::shared_ptr<const wallpaper_engine::DecodedImage> decoded = decodeShared(abs_path, image_index);
            const auto trace_upload_start = std::chrono::steady_clock::now();
            const wallpaper_engine::DecodedImage& image = *decoded;
            if (!image.is_video && image.valid()) {
                const sg_pixel_format pixel_format = toSokolPixelFormat(image.format);
                if (pixel_format != SG_PIXELFORMAT_NONE) {
                    sg_image_desc desc = {};
                    desc.width = (int)image.width;
                    desc.height = (int)image.height;
                    desc.pixel_format = pixel_format;
                    desc.data.mip_levels[0] = {image.pixels.data(), image.pixels.size()};
                    const sg_image uploaded = sg_make_image(&desc);
                    if (load_trace::enabled())
                        LOG_TAG_I("LOAD_TRACE", "texture_decode_ms=%.3f texture_upload_ms=%.3f bytes=%zu %s",
                                  load_trace::milliseconds(trace_upload_start - trace_decode_start),
                                  load_trace::milliseconds(std::chrono::steady_clock::now() - trace_upload_start),
                                  image.pixels.size(), abs_path);
                    return uploaded;
                }
            }
        }

        if (image_index == 0) {
            for (const ActiveVideoTexture& video : video_textures) {
                if (video.path == abs_path) return GfxImage(video.image);
            }
            std::unique_ptr<wallpaper_engine::VideoTexture> video;
            auto prepared = prepared_video_data_.find(abs_path);
            if (prepared != prepared_video_data_.end()) {
                std::vector<uint8_t> first_frame = std::move(prepared->second.first_frame);
                std::unique_ptr<VideoAudioStream> audio = std::move(prepared->second.audio);
                video = std::move(prepared->second.decoder);
                prepared_video_data_.erase(prepared);
                const sg_image image =
                    makeVideoImage(abs_path, std::move(video), std::move(first_frame), std::move(audio), true);
                if (image.id != SG_INVALID_ID) return GfxImage(image);
            } else {
                if (failed_prepared_videos_.count(abs_path)) return {};
                video = wallpaper_engine::VideoTexture::open(abs_path);
                if (video) {
                    const sg_image image = makeVideoImage(abs_path, std::move(video));
                    if (image.id != SG_INVALID_ID) return GfxImage(image);
                }
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

GfxImage AssetManager::resolveMaterialTexture(const char* mat_rel_path, std::string* out_path,
                                              const UserProperties* user_properties) const {
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
        cJSON* user_textures = cJSON_GetObjectItemCaseSensitive(pass, "usertextures");
        cJSON* first_user = cJSON_IsArray(user_textures) ? cJSON_GetArrayItem(user_textures, 0) : nullptr;
        cJSON* user_name = cJSON_IsObject(first_user) ? cJSON_GetObjectItemCaseSensitive(first_user, "name") : nullptr;
        const bool media_thumbnail = cJSON_IsString(user_name) && user_name->valuestring &&
                                     (strcmp(user_name->valuestring, "$mediaThumbnail") == 0 ||
                                      strcmp(user_name->valuestring, "$mediaPreviousThumbnail") == 0);
        if (media_thumbnail) img = resolveTextureInternal(user_name->valuestring, out_path, 0, false);
        // An image the user picked for this slot replaces the material's own texture.
        const std::string* picked = pickedTexture(first_user, user_properties);
        if (picked) img = resolveTextureInternal(picked->c_str(), out_path, 0, false);
        if (!media_thumbnail && !picked && cJSON_IsArray(textures)) {
            cJSON* tex_node = cJSON_GetArrayItem(textures, 0);
            if (cJSON_IsString(tex_node) && tex_node->valuestring && tex_node->valuestring[0] != '\0') {
                const std::string texture_ref = tex_node->valuestring;
                // Render targets (`_rt_*`) are bound by the effect chain at draw time, not loaded as textures.
                if (texture_ref.rfind("_rt_", 0) == 0 || texture_ref.find("/_rt_") != std::string::npos) {
                    cJSON_Delete(mat_json);
                    return img;
                }
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
