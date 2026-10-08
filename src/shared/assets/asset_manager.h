#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "providers/asset_provider.h"
#include "shared/assets/content_bounds.h"
#include "shared/assets/decoded_image.h"
#include "shared/assets/media/video_audio.h"
#include "shared/assets/media/video_texture.h"
#include "shared/audio/audio_engine.h"
#include "shared/core/interfaces.h"
#include "shared/graphics/gfx_resource.h"
#include "sokol_gfx.h"

class Layer;
class TextureDecodeCache;
struct SharedAssets;
namespace wallpaper_engine {
struct SceneDocument;
}

class AssetManager : public IAssetResolver {
   public:
    AssetManager();
    ~AssetManager() override;

    void init(const char* engine_path, const char* wallpaper_path);

    // Reference the process-wide shared assets (install/internal providers and
    // their decode cache). Built once by the app.
    void attachShared(SharedAssets* shared);
    // Point at one wallpaper's content (its provider and decode cache).
    void initWallpaper(const char* wallpaper_path);
    // Worker preparation: initialize wallpaper provider, prefetch package textures, then wait for CPU decodes.
    void prepareWallpaper(const char* wallpaper_root);
    bool prepareSceneAssets(const wallpaper_engine::SceneDocument& document);
    // Worker preparation for media. Decoder setup is CPU-only; graphics import setup happens during resolution.
    bool prepareVideo(const char* video_path);
    bool textureReady(const char* path, int image_index = 0) const;

    void setAudioGroup(AudioEngine::GroupId group) {
        audio_group_ = group;
    }

    void prefetchPackageTextures() const;
    void releaseDecodedTextures() const;
    void updateVideoTextures(float elapsed_seconds, const std::vector<Layer*>& active_layers = {});
    void clearVideoTextures();
    void setVideoPlayback(float rate, float volume);
    void setVideoPaused(bool paused);
    bool resolvePath(const char* rel_path, char* out_abs_path, int max_len) const override;

    GfxImage resolveTexture(const char* name, std::string* out_path = nullptr, int image_index = 0) const override;
    GfxImage resolveMaterialTexture(const char* mat_rel_path, std::string* out_path = nullptr) const override;
    // Region of a decoded .tex that holds visible texels (invalid for videos and formats it cannot measure).
    content_bounds::Rect textureContentBounds(const char* abs_path) const;
    // True when every texel of the decoded texture is fully opaque.
    bool textureIsOpaque(const char* abs_path) const;

    struct ActiveVideoTexture {
        std::string path;
        sg_image image = {};
        std::unique_ptr<wallpaper_engine::VideoTexture> decoder;
        float elapsed_seconds = 0.0f;
        std::unique_ptr<VideoAudioStream> audio;
        AudioEngine::StreamHandle audio_stream = AudioEngine::kInvalidStream;
        uint32_t audio_loop_seen = 0;
        float rate = 1.0f;      // set by scripts (IVideoTexture.rate), on top of the global video rate
        double position = 0.0;  // seconds into the current pass through the file
        uint32_t position_loop = 0;
    };

    // The playback state scripts read and write for a video's decoder.
    ActiveVideoTexture* findVideoTexture(const wallpaper_engine::VideoTexture* decoder) {
        for (ActiveVideoTexture& video : video_textures)
            if (video.decoder.get() == decoder) return &video;
        return nullptr;
    }

    const std::vector<ActiveVideoTexture>& getVideoTextures() const {
        return video_textures;
    }

    const ActiveVideoTexture* findVideoTexture(sg_image img) const;
    const ActiveVideoTexture* findVideoTexture(const std::string& path) const;

    const std::string& getEnginePath() const {
        return engine_path;
    }
    const std::string& getWallpaperPath() const {
        return wallpaper_path;
    }

   private:
    std::string engine_path;
    std::string wallpaper_path;

    std::unique_ptr<WallpaperAssetProvider> wallpaper_provider;
    SharedAssets* shared_ = nullptr;
    // Temporary: owns a SharedAssets until the app supplies a process-wide one
    // (removed once every caller uses attachShared).
    std::unique_ptr<SharedAssets> owned_shared_;

    // Analysis of a decoded texture runs once per path, not once per layer that uses it.
    mutable std::unordered_map<std::string, content_bounds::Rect> content_bounds_cache_;
    mutable std::unordered_map<std::string, bool> opacity_cache_;
    mutable std::vector<ActiveVideoTexture> video_textures;
    struct PreparedVideo {
        std::unique_ptr<wallpaper_engine::VideoTexture> decoder;
        std::vector<uint8_t> first_frame;
        std::unique_ptr<VideoAudioStream> audio;
    };
    mutable std::unordered_map<std::string, PreparedVideo> prepared_video_data_;
    mutable std::unordered_set<std::string> failed_prepared_videos_;
    float video_rate_ = 1.0f;
    float video_volume_ = 1.0f;
    bool video_paused_ = false;
    AudioEngine::GroupId audio_group_ = AudioEngine::kDefaultGroup;

    sg_image makeVideoImage(const char* path, std::unique_ptr<wallpaper_engine::VideoTexture> video,
                            std::vector<uint8_t> first_frame = {}, std::unique_ptr<VideoAudioStream> audio = {},
                            bool audio_prepared = false) const;
    void addVideoTexture(const char* path, sg_image image, std::unique_ptr<wallpaper_engine::VideoTexture> video,
                         std::unique_ptr<VideoAudioStream> audio = {}, bool audio_prepared = false) const;

    GfxImage resolveTextureInternal(const char* name, std::string* out_path, int image_index,
                                    bool warn_on_failure) const;

    std::unique_ptr<TextureDecodeCache> wallpaper_decode_cache_;
    const TextureDecodeCache& decodeCacheFor(const char* abs_path) const;
    std::shared_ptr<const wallpaper_engine::DecodedImage> decodeShared(const char* abs_path, int image_index) const;
};

#endif  // ASSET_MANAGER_H
