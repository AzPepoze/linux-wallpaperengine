#ifndef MEDIA_EVENTS_H
#define MEDIA_EVENTS_H

#include <memory>

#include "scene_script.h"
#include "shared/media/media_session.h"

namespace wallpaper_engine {

// Translates one media event into the SceneScript hook name, event object and whether it is sticky.
ScriptEvent toScriptEvent(const MediaEvent& event, const char*& hook, bool& sticky);

// Feeds media events to the loaded scripts. The source is created on demand and started only once a script exports
// one of the media hooks, so wallpapers that do not listen never spawn the D-Bus thread.
class MediaScriptBridge {
   public:
    explicit MediaScriptBridge(std::unique_ptr<MediaSource> source = nullptr);
    ~MediaScriptBridge();
    MediaScriptBridge(const MediaScriptBridge&) = delete;
    MediaScriptBridge& operator=(const MediaScriptBridge&) = delete;

    // Main thread, once per frame.
    void update();

   private:
    std::unique_ptr<MediaSource> source_;
    bool started_ = false;
};

}  // namespace wallpaper_engine

#endif  // MEDIA_EVENTS_H
