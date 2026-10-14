#pragma once

#include "gfx_resource.h"
#include "render.h"
#include "sokol_gfx.h"

// SMAA 1x anti-aliasing (upstream code in lib/smaa). Three passes write into targets the pass owns.
class SmaaPass {
   public:
    // Returns the anti-aliased view, or an invalid view when the pass could not run.
    sg_view apply(renderer_t* r, sg_view source, int width, int height);

   private:
    bool ensureLookupTables();
    bool ensurePipelines();
    bool ensureTargets(int width, int height);
    void draw(renderer_t* r, sg_view target, sg_pipeline pipeline, const sg_view* inputs, int input_count, int width,
              int height);

    GfxImage area_image_;
    GfxView area_view_;
    GfxImage search_image_;
    GfxView search_view_;

    GfxPipeline edge_pipeline_;
    GfxPipeline blend_pipeline_;
    GfxPipeline neighborhood_pipeline_;

    GfxImage edges_image_;
    GfxView edges_view_;
    GfxView edges_attachment_;
    GfxImage blend_image_;
    GfxView blend_view_;
    GfxView blend_attachment_;
    GfxImage output_image_;
    GfxView output_view_;
    GfxView output_attachment_;
    int target_width_ = 0;
    int target_height_ = 0;
};
