#include "media_events.h"

#include "script_engine.h"

namespace wallpaper_engine {

namespace {

ScriptEvent statusEvent(const MediaEvent& event) {
    return {{"enabled", ScriptValue::makeBool(event.enabled)}};
}

ScriptEvent playbackEvent(const MediaEvent& event) {
    const double state = event.state == PlaybackState::Playing ? 1.0 : event.state == PlaybackState::Paused ? 2.0 : 0.0;
    return {{"state", ScriptValue::makeNumber(state)}};
}

ScriptEvent propertiesEvent(const MediaEvent& event) {
    const MediaProperties& properties = event.properties;
    return {{"title", ScriptValue::makeString(properties.title)},
            {"artist", ScriptValue::makeString(properties.artist)},
            {"albumTitle", ScriptValue::makeString(properties.albumTitle)},
            {"albumArtist", ScriptValue::makeString(properties.albumArtist)},
            {"subTitle", ScriptValue::makeString(properties.subTitle)},
            {"genres", ScriptValue::makeString(properties.genres)},
            {"contentType", ScriptValue::makeString(properties.contentType)}};
}

ScriptEvent thumbnailEvent(const MediaEvent& event) {
    const ThumbnailColors& thumbnail = event.thumbnail;
    auto color = [](const float* value) {
        return value ? ScriptValue::makeVec3(value[0], value[1], value[2]) : ScriptValue::makeVec3(0.0, 0.0, 0.0);
    };
    return {{"hasThumbnail", ScriptValue::makeBool(thumbnail.has_thumbnail)},
            {"primaryColor", color(thumbnail.has_thumbnail ? thumbnail.primary : nullptr)},
            {"secondaryColor", color(thumbnail.has_thumbnail ? thumbnail.secondary : nullptr)},
            {"tertiaryColor", color(thumbnail.has_thumbnail ? thumbnail.tertiary : nullptr)},
            {"textColor", color(thumbnail.has_thumbnail ? thumbnail.text : nullptr)},
            {"highContrastColor", color(thumbnail.has_thumbnail ? thumbnail.high_contrast : nullptr)}};
}

ScriptEvent timelineEvent(const MediaEvent& event) {
    return {{"position", ScriptValue::makeNumber(event.position)},
            {"duration", ScriptValue::makeNumber(event.duration)}};
}

}  // namespace

ScriptEvent toScriptEvent(const MediaEvent& event, const char*& hook, bool& sticky) {
    sticky = true;
    switch (event.kind) {
        case MediaEvent::Kind::Status:
            hook = "mediaStatusChanged";
            return statusEvent(event);
        case MediaEvent::Kind::Playback:
            hook = "mediaPlaybackChanged";
            return playbackEvent(event);
        case MediaEvent::Kind::Properties:
            hook = "mediaPropertiesChanged";
            return propertiesEvent(event);
        case MediaEvent::Kind::Thumbnail:
            hook = "mediaThumbnailChanged";
            return thumbnailEvent(event);
        case MediaEvent::Kind::Timeline:
            hook = "mediaTimelineChanged";
            sticky = false;
            return timelineEvent(event);
    }
    hook = "mediaStatusChanged";
    return statusEvent(event);
}

MediaScriptBridge::MediaScriptBridge(std::unique_ptr<MediaSource> source)
    : source_(source ? std::move(source) : createMprisMediaSource()) {}

MediaScriptBridge::~MediaScriptBridge() {
    if (source_) source_->stop();
}

void MediaScriptBridge::update(bool wants_thumbnail, const std::function<void(const ThumbnailColors&)>& on_thumbnail) {
    if (!started_) {
        if (!wants_thumbnail) {
            // Scanning every script for its hooks is not free, so only look again when scripts were loaded or freed.
            ScriptEngine& engine = ScriptEngine::instance();
            if (engine.scriptCount() == checked_script_count_) return;
            checked_script_count_ = engine.scriptCount();
            if (!engine.anyScriptExports({"mediaStatusChanged", "mediaPlaybackChanged", "mediaPropertiesChanged",
                                          "mediaThumbnailChanged", "mediaTimelineChanged"}))
                return;
        }
        source_->start();
        started_ = true;
    }
    for (const MediaEvent& event : source_->poll()) {
        if (event.kind == MediaEvent::Kind::Thumbnail && on_thumbnail) on_thumbnail(event.thumbnail);
        const char* hook = nullptr;
        bool sticky = false;
        const ScriptEvent script_event = toScriptEvent(event, hook, sticky);
        ScriptEngine::instance().broadcast(hook, script_event, sticky);
    }
}

}  // namespace wallpaper_engine
