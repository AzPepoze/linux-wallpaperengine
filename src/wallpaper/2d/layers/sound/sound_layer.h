#ifndef SOUND_LAYER_H
#define SOUND_LAYER_H

#include <random>
#include <string>
#include <vector>

#include "shared/audio/audio_engine.h"
#include "wallpaper/2d/layers/layer.h"

// Non-visual scene object that drives a playlist of sound files through the AudioEngine.
class SoundLayer : public Layer {
   public:
    SoundLayer(const char* name, const wallpaper_engine::SoundObjectDocument& doc);
    ~SoundLayer() override;

    static SoundLayer* createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx);

    void update(float dt, EngineContext& ctx) override;
    void draw(EngineContext&) override {}
    void start() override;
    void stop() override;
    void setVisible(bool v) override;

    bool playing() const {
        return started && current != AudioEngine::kInvalidSound;
    }
    float volume() const {
        return doc.volume;
    }
    void setVolume(float volume);

   private:
    void playCurrent();
    void advance();
    void applyVolume();

    wallpaper_engine::SoundObjectDocument doc;
    std::vector<std::string> paths;
    AudioEngine::GroupId group_ = AudioEngine::kDefaultGroup;
    AudioEngine::SoundHandle current = AudioEngine::kInvalidSound;
    int current_index = 0;
    float timer = 0.0f;
    float pending_delay = 0.0f;
    bool started = false;
    bool finished = false;
    std::mt19937 rng;
};

#endif  // SOUND_LAYER_H
