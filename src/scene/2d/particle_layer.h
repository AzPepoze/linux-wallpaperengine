#ifndef PARTICLE_LAYER_H
#define PARTICLE_LAYER_H

#include "../layer.h"
#include "particles.h"

class EngineContext;

class ParticleLayer : public Layer {
   public:
    ParticleSystem* ps;

    ParticleLayer(const char* name, ParticleSystem* ps);
    virtual ~ParticleLayer();

    static ParticleLayer* createFromJSON(cJSON* node, EngineContext& ctx);

    void update(float dt, EngineContext& ctx) override;
    void draw(EngineContext& ctx) override;
    void drawDebug(EngineContext& ctx) override;
};

#endif  // PARTICLE_LAYER_H
