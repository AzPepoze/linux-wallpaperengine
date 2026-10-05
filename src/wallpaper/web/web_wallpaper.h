#ifndef WEB_WALLPAPER_H
#define WEB_WALLPAPER_H

#include <stdint.h>
#include <sys/types.h>

#include <string>

#include "shared/graphics/backend/gpu_bgra.h"
#include "sokol_gfx.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/web/web_ipc.h"

// Web wallpapers are rendered by an optional out-of-process QtWebEngine helper
// (see web_renderer_main.cpp) that streams BGRA frames through shared memory.
// The host side only needs POSIX, so the core build carries no Qt dependency.
class WebWallpaper : public Scene2DWallpaper {
   public:
    explicit WebWallpaper(EngineContext& ctx);
    ~WebWallpaper() override;

    WallpaperType getType() const override {
        return WallpaperType::Web;
    }
    bool load(const std::string& path, EngineContext& ctx) override;
    void update(float dt, EngineContext& ctx) override;
    void handleInput(const sapp_event* event, EngineContext& ctx) override;
    void clear() override;

   private:
    void pollChild();
    void pollDmaBuf();
    void stopChild();

    pid_t child_pid_ = -1;
    int ctrl_fd_ = -1;
    int memfd_ = -1;
    WebFrameBuffer* frame_ = nullptr;
    size_t shm_size_ = 0;
    sg_image image_ = {};
    uint64_t last_frame_counter_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    bool dmabuf_offer_received_ = false;
    bool dmabuf_ready_ = false;
    uint64_t last_published_ = 0;
    ImportedBgraSurface surfaces_[kWebDmaBufBuffers];
};

#endif  // WEB_WALLPAPER_H
