#ifndef INTERFACES_H
#define INTERFACES_H

#include <string>

#include "shared/graphics/gfx_resource.h"

class EngineContext;

class ILayer {
   public:
    virtual ~ILayer() = default;
    virtual void update(float dt, EngineContext& ctx) = 0;
    virtual void draw(EngineContext& ctx) = 0;
    virtual void drawDebug(EngineContext&) {}
    virtual const std::string& get_name() const = 0;
    virtual bool is_visible() const = 0;
};

class IAssetResolver {
   public:
    virtual ~IAssetResolver() = default;
    virtual GfxImage resolveTexture(const char* name, std::string* out_path = nullptr, int image_index = 0) const = 0;
    virtual GfxImage resolveMaterialTexture(const char* mat_rel_path, std::string* out_path = nullptr) const = 0;
    virtual bool resolvePath(const char* rel_path, char* out_abs_path, int max_len) const = 0;
};

#endif  // INTERFACES_H
