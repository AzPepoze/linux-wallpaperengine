#ifndef LAYER_H
#define LAYER_H

#include <string>

#include "../../libs/linmath.h"
#include "../../libs/sokol/sokol_gfx.h"

class Layer {
   public:
    std::string name;
    bool visible = true;
    vec3 origin = {0, 0, 0};
    vec2 size = {0, 0};
    vec3 scale = {1, 1, 1};
    float rotation = 0.0f;
    float tint[4] = {1, 1, 1, 1};
    vec2 parallax = {0, 0};

    Layer(const char* name) : name(name) {}
    virtual ~Layer() {}

    // Disable copy for now to satisfy cppcheck (since we manage resources in subclasses)
    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;

    virtual void update(float dt) = 0;
    virtual void draw() = 0;
    virtual void showInspector() = 0;
};

#endif  // LAYER_H
