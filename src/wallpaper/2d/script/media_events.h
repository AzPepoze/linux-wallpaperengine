#ifndef MEDIA_EVENTS_H
#define MEDIA_EVENTS_H

#include <functional>
#include <memory>

#include "scene_script.h"
#include "shared/media/media_session.h"

namespace wallpaper_engine {

ScriptEvent toScriptEvent(const MediaEvent& event, const char*& hook, bool& sticky);

// The media source starts only once a script exports a media hook, so other wallpapers never spawn the D-Bus thread.
class MediaScriptBridge {
   public:
    explicit MediaScriptBridge(std::unique_ptr<MediaSource> source = nullptr);
    ~MediaScriptBridge();
    MediaScriptBridge(const MediaScriptBridge&) = delete;
    MediaScriptBridge& operator=(const MediaScriptBridge&) = delete;

    // wants_thumbnail starts the source even without a listener; on_thumbnail gets every event.
    void update(bool wants_thumbnail = false, const std::function<void(const ThumbnailColors&)>& on_thumbnail = {});

   private:
    std::unique_ptr<MediaSource> source_;
    bool started_ = false;
    size_t checked_script_count_ = static_cast<size_t>(-1);  // scripts present at the last "does anyone listen" scan
};

}  // namespace wallpaper_engine

#endif  // MEDIA_EVENTS_H
