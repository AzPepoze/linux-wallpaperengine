#ifndef SCENE_2D_RUNTIME_H
#define SCENE_2D_RUNTIME_H

#include <chrono>

#include "shared/core/engine_context.h"
#include "shared/graphics/gfx_resource.h"
#include "shared/graphics/smaa_pass.h"

class Scene2DRuntime {
   public:
    explicit Scene2DRuntime(EngineContext& ctx) : ctx(ctx) {}

    void init();
    void precompileBlendModes();
    void beginPrewarm();
    bool stepPrewarm(std::chrono::milliseconds budget);
    void update(float dt);
    void draw();
    void drawParticleDiagnostics();
    void present();
    bool requiresOffscreenComposition() const;
    void updateViewport();
    void setOutputViewport(int x, int y, int width, int height);
    void resetOutputViewport();
    void clearScene();
    void cleanup();
    bool stepCleanup();

    // Forces offscreen scene targets so composedView() has a valid texture.
    void setForceOffscreen(bool on) {
        force_offscreen_ = on;
    }
    bool forceOffscreen() const {
        return force_offscreen_;
    }
    // The composed scene texture after draw(), or an empty view when unavailable.
    sg_view composedView() const;
    // The image backing composedView(), for binding during a copy.
    sg_image composedImage() const;

   private:
    struct SceneTarget {
        GfxView attachment_view;
        GfxView texture_view;
        GfxImage image;
        // With MSAA, passes draw into msaa_image and resolve into `image`, which stays the sampled texture.
        GfxImage msaa_image;
        GfxView msaa_view;
        GfxView resolve_view;
        int width = 0;
        int height = 0;
        int samples = 1;
        sg_pixel_format pixel_format = SG_PIXELFORMAT_NONE;

        bool multisampled() const {
            return msaa_view.id != SG_INVALID_ID;
        }

        void reset() {
            msaa_view = {};
            resolve_view = {};
            msaa_image = {};
            samples = 1;
            if (attachment_view.id != SG_INVALID_ID) sg_destroy_view(attachment_view);
            if (texture_view.id != SG_INVALID_ID) sg_destroy_view(texture_view);
            if (image.id != SG_INVALID_ID) sg_destroy_image(image);
            attachment_view = {};
            texture_view = {};
            image = {};
            width = 0;
            height = 0;
            pixel_format = SG_PIXELFORMAT_NONE;
        }

        bool createMultisampled(int w, int h, sg_pixel_format fmt, int sample_count) {
            sg_image_desc msaa_desc = {};
            msaa_desc.usage.color_attachment = true;
            msaa_desc.width = w;
            msaa_desc.height = h;
            msaa_desc.pixel_format = fmt;
            msaa_desc.sample_count = sample_count;
            msaa_image = sg_make_image(&msaa_desc);
            if (sg_query_image_state(msaa_image) != SG_RESOURCESTATE_VALID) return false;

            sg_view_desc msaa_view_desc = {};
            msaa_view_desc.color_attachment.image = msaa_image;
            msaa_view = sg_make_view(&msaa_view_desc);
            sg_view_desc resolve_desc = {};
            resolve_desc.resolve_attachment.image = image;
            resolve_view = sg_make_view(&resolve_desc);
            return msaa_view.id != SG_INVALID_ID && resolve_view.id != SG_INVALID_ID;
        }

        bool create(int w, int h, sg_pixel_format fmt, int sample_count = 1) {
            reset();
            sg_image_desc image_desc = {};
            image_desc.usage.color_attachment = true;
            image_desc.usage.resolve_attachment = sample_count > 1;
            image_desc.width = w;
            image_desc.height = h;
            image_desc.pixel_format = fmt;
            image = sg_make_image(&image_desc);
            if (image.id == SG_INVALID_ID && fmt != SG_PIXELFORMAT_RGBA8) {
                image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
                image = sg_make_image(&image_desc);
            }
            if (image.id == SG_INVALID_ID) return false;

            sg_view_desc texture_desc = {};
            texture_desc.texture.image = image;
            texture_view = sg_make_view(&texture_desc);

            sg_view_desc attachment_desc = {};
            attachment_desc.color_attachment.image = image;
            attachment_view = sg_make_view(&attachment_desc);

            if (texture_view.id == SG_INVALID_ID || attachment_view.id == SG_INVALID_ID ||
                (sample_count > 1 && !createMultisampled(w, h, image_desc.pixel_format, sample_count))) {
                reset();
                return false;
            }

            width = w;
            height = h;
            samples = sample_count;
            pixel_format = image_desc.pixel_format;
            return true;
        }
    };

    // Depth buffer shared by every scene pass while the scene has 3D model layers.
    struct DepthTarget {
        GfxImage image;
        GfxView attachment_view;
        int width = 0;
        int height = 0;
        int samples = 1;

        bool valid() const {
            return attachment_view.id != SG_INVALID_ID;
        }
        void reset() {
            attachment_view = {};
            image = {};
            width = 0;
            height = 0;
            samples = 1;
        }
        bool create(int w, int h, int sample_count = 1) {
            reset();
            sg_image_desc image_desc = {};
            image_desc.usage.depth_stencil_attachment = true;
            image_desc.width = w;
            image_desc.height = h;
            image_desc.pixel_format = SG_PIXELFORMAT_DEPTH;
            image_desc.sample_count = sample_count;
            image = sg_make_image(&image_desc);
            if (image.id == SG_INVALID_ID) return false;

            sg_view_desc attachment_desc = {};
            attachment_desc.depth_stencil_attachment.image = image;
            attachment_view = sg_make_view(&attachment_desc);
            if (!valid()) {
                reset();
                return false;
            }
            width = w;
            height = h;
            samples = sample_count;
            return true;
        }
    };

    EngineContext& ctx;
    int output_x = 0;
    int output_y = 0;
    int output_width = 0;
    int output_height = 0;
    bool first_frame_drawn_ = false;
    bool force_offscreen_ = false;
    SceneTarget scene_targets[2];
    SceneTarget bloom_targets[2];
    DepthTarget depth_target;
    // _rt_Reflection: reflected models seen in the floor mirror, only while the scene has some.
    SceneTarget reflection_target;
    DepthTarget reflection_depth;
    std::vector<SceneTarget> hdr_bloom_levels;
    int scene_output_index = -1;
    // Anti-aliased frame to present when --antialias smaa is set; invalid otherwise.
    SmaaPass smaa_;
    sg_view presented_view_ = {SG_INVALID_ID};
    // Samples the GPU grants for --antialias msaa; scene_samples_ is what the current frame uses.
    int msaa_samples_ = 1;
    int scene_samples_ = 1;
    bool float_composition_available_ = true;
    int cleanup_phase_ = 0;
    int prewarm_target_index_ = 0;
    size_t prewarm_image_index_ = 0;
    size_t prewarm_image_target_cursor_ = 0;
    bool prewarm_done_ = false;
    class ShaderPass* bloom_pass_extract = nullptr;
    class ShaderPass* bloom_pass_blur_v = nullptr;
    class ShaderPass* bloom_pass_blur_h = nullptr;
    class ShaderPass* bloom_pass_combine = nullptr;

    void initBloomPipelines();
    void destroyBloomPipelines();
    sg_pixel_format compositionPixelFormat() const;
    bool ensureSceneTargets(int width, int height);
    bool hasDepthLayers() const;
    void updateDepthTarget(int width, int height);
    // Adds the scene depth buffer to a pass that draws layers, keeping its contents.
    void attachDepth(sg_pass& pass) const;
    // Color pass over a scene target: multisampled with a resolve into its texture when the frame uses MSAA.
    sg_pass sceneColorPass(const SceneTarget& target, sg_load_action load, float clear_alpha = 0.0f) const;
    void beginScenePass(const sg_pass& pass);
    void endScenePass();
    bool hasReflectedLayers() const;
    void updateReflectionTarget(int width, int height, sg_pixel_format format);
    void resetReflectionTarget();
    // Draws the reflected models mirrored into _rt_Reflection and binds it to the mirror surfaces.
    void renderReflection();
    bool ensureBloomTargets(int width, int height);
    int renderHdrBloom(int current_target_index, int width, int height);
    int renderBloom(int current_target_index, int width, int height);
    void drawDirect();
    void drawOffscreen();
};

#endif  // SCENE_2D_RUNTIME_H
