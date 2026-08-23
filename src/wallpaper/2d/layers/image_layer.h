#ifndef IMAGE_LAYER_H
#define IMAGE_LAYER_H

#include <map>
#include <string>

#include "shared/graphics/diagnostics/gpu_trace.h"
#include "shared/graphics/gfx_resource.h"
#include "wallpaper/2d/layers/layer.h"

class EngineContext;

class ImageLayer : public Layer {
   public:
    GfxImage img;
    GfxView cached_view;
    bool solid_layer = false;
    bool is_fullscreen = false;
    bool copy_background = false;
    int color_blend_mode = 0;

    ImageLayer(const char* name, GfxImage img);
    virtual ~ImageLayer();

    static ImageLayer* createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx);

    void update(float dt, EngineContext& ctx) override;
    void draw(EngineContext& ctx) override;
    void drawDebug(EngineContext& ctx) override;
    bool requiresSceneColor() const;
    void drawComposite(EngineContext& ctx, sg_view scene_view);

    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;

    wallpaper_engine::VideoTexture* bound_video_decoder = nullptr;

   private:
    struct EffectTarget {
        GfxView attachment_view;
        GfxView texture_view;
        GfxImage image;
        int width = 0;
        int height = 0;
        uint64_t generation = 0;

        void reset(const char* reason = "reset", const char* name = "effect_target") {
            if (image.id != SG_INVALID_ID) {
                gpu_trace_rt_destroy(reason, name, image.id, texture_view.id, attachment_view.id, width, height,
                                     generation);
            }
            attachment_view = {};
            texture_view = {};
            image = {};
            width = 0;
            height = 0;
        }

        bool create(int w, int h, const char* kind = "effect_pingpong", const char* name = "effect_target") {
            reset("recreate", name);
            sg_image_desc image_desc = {};
            image_desc.usage.color_attachment = true;
            image_desc.width = w;
            image_desc.height = h;
            image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
            image_desc.sample_count = 1;
            image = sg_make_image(&image_desc);
            if (image.id == SG_INVALID_ID) return false;

            sg_view_desc texture_desc = {};
            texture_desc.texture.image = image;
            texture_view = sg_make_view(&texture_desc);

            sg_view_desc attachment_desc = {};
            attachment_desc.color_attachment.image = image;
            attachment_view = sg_make_view(&attachment_desc);

            if (texture_view.id == SG_INVALID_ID || attachment_view.id == SG_INVALID_ID) {
                reset("error_rollback", name);
                return false;
            }

            width = w;
            height = h;
            generation = gpu_trace_next_target_generation();
            gpu_trace_rt_create(kind, name, image.id, texture_view.id, attachment_view.id, width, height, generation);
            return true;
        }
    };

    struct NamedRenderTarget {
        EffectTarget buffers[2];
        int write_index = 0;
        int target_width = 0;
        int target_height = 0;
        bool has_rendered = false;
        std::string name;

        bool ensureSize(int w, int h, const std::string& target_name) {
            name = target_name;
            if (target_width == w && target_height == h && buffers[0].image.id != SG_INVALID_ID &&
                buffers[1].image.id != SG_INVALID_ID) {
                return true;
            }
            reset("resize");
            std::string n0 = name + "[0]";
            std::string n1 = name + "[1]";
            if (!buffers[0].create(w, h, "effect_named", n0.c_str()) ||
                !buffers[1].create(w, h, "effect_named", n1.c_str())) {
                reset("error_rollback");
                return false;
            }
            target_width = w;
            target_height = h;
            write_index = 0;
            has_rendered = false;
            return true;
        }

        EffectTarget& currentWrite() {
            return buffers[write_index];
        }
        const EffectTarget& currentRead() const {
            return has_rendered ? buffers[1 - write_index] : buffers[write_index];
        }
        void swap() {
            write_index = 1 - write_index;
            has_rendered = true;
        }
        void reset(const char* reason = "reset") {
            buffers[0].reset(reason, (name + "[0]").c_str());
            buffers[1].reset(reason, (name + "[1]").c_str());
            write_index = 0;
            target_width = 0;
            target_height = 0;
            has_rendered = false;
        }
    };
    void loadMaterial(const char* mat_rel_path, EngineContext& ctx);
    void loadModel(const char* mdl_rel_path, EngineContext& ctx);
    void updateCachedView();
    bool ensureEffectTargets(sg_image source_image = {SG_INVALID_ID});

   public:
    void renderEffectChain(EngineContext& ctx, sg_image src_img = {SG_INVALID_ID}, sg_view src_view = {SG_INVALID_ID});

   private:
    EffectTarget effect_targets[2];
    int effect_target_width = 0;
    int effect_target_height = 0;
    sg_image effect_output_image = {SG_INVALID_ID};
    sg_view effect_output_view = {SG_INVALID_ID};
    std::map<std::string, NamedRenderTarget> named_effect_targets;
    wallpaper_engine::ImageObjectDocument alpha_document;
};

#endif  // IMAGE_LAYER_H
