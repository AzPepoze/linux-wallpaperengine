#ifndef PARTICLE_LAYER_H
#define PARTICLE_LAYER_H

#include "../layer.h"
#include "particles.h"

class ParticleLayer : public Layer {
   public:
    ParticleSystem* ps;

    ParticleLayer(const char* name, ParticleSystem* ps);
    virtual ~ParticleLayer();

    void update(float dt) override;
    void draw() override;
    void showInspector() override;
};

#endif  // PARTICLE_LAYER_H
