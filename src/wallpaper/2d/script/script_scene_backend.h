#ifndef SCRIPT_SCENE_BACKEND_H
#define SCRIPT_SCENE_BACKEND_H

#include <stdint.h>

#include <string>
#include <vector>

// What scripts can see and change of the running scene (`thisLayer`, `thisScene`). Layers are addressed by their
// scene object id; an id of 0 is "none". Implemented by the 2D scene runtime, and faked in tests.
class ScriptSceneBackend {
   public:
    virtual ~ScriptSceneBackend() = default;

    virtual bool layerExists(uint32_t id) = 0;
    virtual std::string layerName(uint32_t id) = 0;

    // Properties: "origin", "scale", "angles" (degrees), "size", "parallaxDepth" as vectors, "visible" as a bool.
    virtual bool getVector(uint32_t id, const std::string& property, double out[3], int& components) = 0;
    virtual bool setVector(uint32_t id, const std::string& property, const double value[3]) = 0;
    virtual bool getBool(uint32_t id, const std::string& property, bool& out) = 0;
    virtual bool setBool(uint32_t id, const std::string& property, bool value) = 0;

    virtual uint32_t parentOf(uint32_t id) = 0;
    virtual std::vector<uint32_t> childrenOf(uint32_t id) = 0;

    virtual uint32_t findLayerByName(const std::string& name) = 0;
    virtual std::vector<uint32_t> allLayers() = 0;  // in scene (draw) order
};

#endif  // SCRIPT_SCENE_BACKEND_H
