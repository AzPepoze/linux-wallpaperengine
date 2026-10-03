#include "sound_layer.h"

#include <algorithm>

#include "shared/core/engine_context.h"
#include "shared/core/logger.h"

namespace {
const char* modeName(wallpaper_engine::SoundPlaybackMode mode) {
    switch (mode) {
        case wallpaper_engine::SoundPlaybackMode::Loop:
            return "loop";
        case wallpaper_engine::SoundPlaybackMode::Random:
            return "random";
        default:
            return "single";
    }
}
}  // namespace

SoundLayer::SoundLayer(const char* name, const wallpaper_engine::SoundObjectDocument& doc)
    : Layer(name), doc(doc), rng(std::random_device{}()) {}

SoundLayer::~SoundLayer() {
    stop();
}

SoundLayer* SoundLayer::createFromDocument(const wallpaper_engine::SceneObjectDocument& object, EngineContext& ctx) {
    if (object.sound.sounds.empty()) return nullptr;

    auto* layer = new SoundLayer(object.name.empty() ? "Sound Layer" : object.name.c_str(), object.sound);
    layer->initFromDocument(object, ctx);
    if (layer->name.empty()) layer->name = "Sound Layer";
    for (const std::string& sound : object.sound.sounds) {
        char resolved[1024];
        if (ctx.asset_mgr.resolvePath(sound.c_str(), resolved, sizeof(resolved))) {
            layer->paths.emplace_back(resolved);
        } else {
            LOG_TAG_W("SOUND", "Sound file not found: %s", sound.c_str());
        }
    }
    if (layer->paths.empty()) {
        delete layer;
        return nullptr;
    }

    if (!layer->doc.start_silent) layer->start();
    return layer;
}

void SoundLayer::start() {
    if (started) return;
    started = true;
    finished = false;
    timer = 0.0f;
    if (!AudioEngine::instance().isAvailable()) {
        finished = true;
        return;
    }
    if (doc.playback_mode == wallpaper_engine::SoundPlaybackMode::Random && paths.size() > 1) {
        current_index = (int)(rng() % paths.size());
    } else {
        current_index = 0;
    }
    playCurrent();
}

void SoundLayer::stop() {
    if (current != AudioEngine::kInvalidSound) {
        AudioEngine::instance().stop(current);
        current = AudioEngine::kInvalidSound;
    }
    started = false;
}

void SoundLayer::setVisible(bool v) {
    if (visible == v) return;
    visible = v;
    applyVolume();
}

void SoundLayer::applyVolume() {
    if (current == AudioEngine::kInvalidSound) return;
    if (!AudioEngine::instance().isAvailable()) return;
    AudioEngine::instance().setVolume(current, (doc.mute || !visible) ? 0.0f : doc.volume);
}

void SoundLayer::playCurrent() {
    if (!AudioEngine::instance().isAvailable()) return;
    if (current_index < 0 || current_index >= (int)paths.size()) return;
    const bool loop = doc.playback_mode == wallpaper_engine::SoundPlaybackMode::Loop && paths.size() == 1;
    current = AudioEngine::instance().play(paths[current_index], loop, (doc.mute || !visible) ? 0.0f : doc.volume);
    pending_delay = 0.0f;
    timer = 0.0f;
    if (!loop && doc.playback_mode != wallpaper_engine::SoundPlaybackMode::Loop) {
        const float lo = std::min(doc.min_time, doc.max_time);
        const float hi = std::max(doc.min_time, doc.max_time);
        std::uniform_real_distribution<float> distribution(lo, hi);
        pending_delay = distribution(rng);
    }
    LOG_TAG_I("SOUND", "Playing '%s' (mode=%s, %zu track(s))", paths[current_index].c_str(),
              modeName(doc.playback_mode), paths.size());
}

void SoundLayer::advance() {
    switch (doc.playback_mode) {
        case wallpaper_engine::SoundPlaybackMode::Loop:
            current_index = (current_index + 1) % (int)paths.size();
            break;
        case wallpaper_engine::SoundPlaybackMode::Random:
            if (paths.size() > 1) {
                const int previous = current_index;
                for (int attempt = 0; attempt < 4 && current_index == previous; ++attempt)
                    current_index = (int)(rng() % paths.size());
            }
            break;
        case wallpaper_engine::SoundPlaybackMode::Single:
        default:
            if (current_index + 1 >= (int)paths.size()) {
                finished = true;
                if (current != AudioEngine::kInvalidSound) {
                    AudioEngine::instance().stop(current);
                    current = AudioEngine::kInvalidSound;
                }
                return;
            }
            ++current_index;
            break;
    }
    playCurrent();
}

void SoundLayer::update(float dt, EngineContext& ctx) {
    (void)ctx;
    if (!started || finished || current == AudioEngine::kInvalidSound) return;
    if (doc.playback_mode == wallpaper_engine::SoundPlaybackMode::Loop && paths.size() == 1) return;
    if (AudioEngine::instance().isPlaying(current)) {
        timer = 0.0f;
        return;
    }

    timer += dt;
    if (timer < pending_delay) return;
    advance();
}
