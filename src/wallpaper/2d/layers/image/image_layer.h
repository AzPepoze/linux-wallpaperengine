#ifndef IMAGE_LAYER_H
#define IMAGE_LAYER_H

#include <array>
#include <map>
#include <string>
#include <vector>

#include "shared/assets/tex_decoder.h"
#include "shared/graphics/gfx_resource.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/parser/scene_document.h"
#include "wallpaper/2d/puppet/mdl_parser.h"
#include "wallpaper/2d/puppet/puppet_pose.h"

class EngineContext;
class Effect;
class IRenderObserver;
class ShaderPass;

class ImageLayer : public Layer {
   public:
    GfxImage img;
    GfxView cached_view;
    bool solid_layer = false;
    bool is_fullscreen = false;
    bool is_compose_region = false;
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
    void renderRegionEffectChain(EngineContext& ctx, sg_image scene_image, sg_view scene_view);

    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;

    bool usesTexture(sg_image i) const override {
        return img.id == i.id;
    }
    bool usesTexturePath(const std::string& p) const override {
        return !path.empty() && (path == p || path.find(p) != std::string::npos);
    }

    wallpaper_engine::VideoTexture* bound_video_decoder = nullptr;

   private:
    struct EffectTarget {
        GfxView attachment_view;
        GfxView texture_view;
        GfxImage image;
        int width = 0;
        int height = 0;

        void reset() {
            attachment_view = {};
            texture_view = {};
            image = {};
            width = 0;
            height = 0;
        }

        bool create(int w, int h) {
            reset();
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
                reset();
                return false;
            }

            width = w;
            height = h;
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
            reset();
            if (!buffers[0].create(w, h) || !buffers[1].create(w, h)) {
                reset();
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
        void reset() {
            buffers[0].reset();
            buffers[1].reset();
            write_index = 0;
            target_width = 0;
            target_height = 0;
            has_rendered = false;
        }
    };
    void loadMaterial(const char* mat_rel_path, EngineContext& ctx);
    void loadModel(const char* mdl_rel_path, EngineContext& ctx);
    void loadPuppet(const char* mdl_rel_path, EngineContext& ctx);
    void setPuppetLayers();
    bool ensurePuppetTarget(int width, int height);
    bool renderPuppet(EngineContext& ctx);
    void updatePuppetPositions(int width, int height);
    void updateCachedView();
    void updateAnimatedFrame(EngineContext& ctx);
    bool ensureEffectTargets(sg_image source_image = {SG_INVALID_ID});

   public:
    void renderEffectChain(EngineContext& ctx, sg_image src_img = {SG_INVALID_ID}, sg_view src_view = {SG_INVALID_ID});

   private:
    // State threaded through the passes of one effect chain.
    struct ChainState {
        sg_image layer_source_image = {SG_INVALID_ID};
        sg_view layer_source_view = {SG_INVALID_ID};
        sg_image input_image = {SG_INVALID_ID};
        sg_view input_view = {SG_INVALID_ID};
        // Passes that render into a named target do not advance the layer image that an explicit `previous` binding
        // reads.
        sg_image chain_image = {SG_INVALID_ID};
        sg_view chain_view = {SG_INVALID_ID};
        int write_index = 0;
        bool rendered_any = false;
        int draw_order = 0;
    };

    // The images one pass samples: slot 0 and the explicit render-target bindings for slots 1..11.
    struct PassInputs {
        sg_image image = {SG_INVALID_ID};
        sg_view view = {SG_INVALID_ID};
        std::array<sg_image, 11> override_images;
        std::array<sg_view, 11> override_views;
        bool has_overrides = false;
    };

    PassInputs resolvePassInputs(const ShaderPass& pass, const ChainState& state);
    // Moves on after a pass wrote its target: the next pass reads what this one produced.
    void advanceChain(ChainState& state, NamedRenderTarget* named_target);
    void tracePass(IRenderObserver& diag, EngineContext& ctx, const Effect& effect, const ShaderPass& pass,
                   int effect_index, int pass_index, const PassInputs& inputs, ChainState& state,
                   NamedRenderTarget* named_target, sg_image output_image, int target_width, int target_height);

    EffectTarget effect_targets[2];
    EffectTarget region_source;
    EffectTarget animated_frame;
    wallpaper_engine::TextureMetadata texture_metadata;
    GfxImage animation_page;
    GfxView animation_page_view;
    uint32_t animation_page_index = 0;
    const wallpaper_engine::TextureAnimationFrame* current_texture_frame = nullptr;
    int effect_target_width = 0;
    int effect_target_height = 0;
    sg_image effect_output_image = {SG_INVALID_ID};
    sg_view effect_output_view = {SG_INVALID_ID};
    std::map<std::string, NamedRenderTarget> named_effect_targets;

   protected:
    struct ScreenRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float rotation = 0.0f;
    };
    // Sprite placement in screen pixels. Text layers override this to anchor
    // the sprite to the alignment corner instead of the centre.
    virtual ScreenRect screenRect(EngineContext& ctx) const;

    wallpaper_engine::ImageObjectDocument alpha_document;

   private:
    // Puppet mesh: the parsed model, its rest/skinned positions and the
    // off-screen target the mesh is drawn into before the effect chain runs.
    wallpaper_engine::MdlModel puppet;
    bool has_puppet_mesh = false;
    bool puppet_resolved = false;
    GfxBuffer puppet_position_buffer;
    GfxBuffer puppet_uv_buffer;
    GfxBuffer puppet_index_buffer;
    int puppet_index_count = 0;
    std::vector<float> puppet_skinned;
    std::vector<float> puppet_positions;
    EffectTarget puppet_target;
    EffectTarget puppet_straight;
    wallpaper_engine::PuppetPose puppet_pose;
    std::vector<wallpaper_engine::PuppetAnimationLayer> puppet_layers;
};

#endif  // IMAGE_LAYER_H
