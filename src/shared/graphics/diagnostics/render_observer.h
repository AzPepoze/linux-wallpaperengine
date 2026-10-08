#ifndef RENDER_OBSERVER_H
#define RENDER_OBSERVER_H

#include <string>

#include "render_graph.h"
#include "sokol_gfx.h"

struct ShaderDump;
struct PassUniformProvenance;

// Release builds get the no-op defaults; see renderObserver().
class IRenderObserver {
   public:
    virtual ~IRenderObserver() = default;

    virtual bool isCapturingFrame() const {
        return false;
    }
    virtual bool isTracingPasses() const {
        return false;
    }
    virtual bool isCollectingShaderInfo() const {
        return false;
    }

    virtual bool isEffectIsolated(int /*effect_index*/, const std::string& /*effect_path*/) const {
        return true;
    }
    virtual bool isEffectDisabled(int /*effect_index*/, const std::string& /*effect_path*/) const {
        return false;
    }
    virtual bool isPassDisabled(int /*pass_index*/) const {
        return false;
    }
    virtual bool shouldStopAfterPass(int /*pass_index*/) const {
        return false;
    }

    virtual void onSourceImage(int /*effect_index*/, sg_image /*img*/, int /*width*/, int /*height*/) {}
    virtual void recordPass(PassTraceEntry /*trace*/, sg_image /*out_img*/) {}
    virtual void onLayerFinalImage(int /*effect_index*/, sg_image /*img*/, int /*width*/, int /*height*/) {}
    virtual void recordSceneStage(const std::string& /*stage_name*/, sg_image /*img*/, sg_view /*texture_view*/,
                                  sg_view /*attachment_view*/) {}

    virtual void registerShaderDump(const ShaderDump& /*dump*/) {}
    virtual void registerUniformProvenance(const PassUniformProvenance& /*prov*/) {}

    // Lets the inspector's shader debug views rewrite a pass's fragment source.
    virtual std::string overrideFragmentSource(const std::string& /*shader_name*/, const std::string& fs_source,
                                               int /*view_mode*/, int /*step*/) const {
        return fs_source;
    }
};

IRenderObserver& renderObserver();

#endif  // RENDER_OBSERVER_H
