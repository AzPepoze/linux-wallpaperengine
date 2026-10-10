#include "wallpaper/2d/layers/image/effect_resolution.h"

#include "test_util.h"

int main() {
    CHECK(effect_resolution::readsPixelPosition("vec2 p = gl_FragCoord.xy;"));
    CHECK(effect_resolution::readsPixelPosition("float d = dFdx(v);"));
    CHECK(!effect_resolution::readsPixelPosition("vec4 c = texSample2D(g_Texture0, v_TexCoord.xy);"));
    CHECK(effect_resolution::isCompositeBinding("_rt_FullFrameBuffer"));
    CHECK(effect_resolution::isCompositeBinding("_rt_imageLayerComposite_12_a"));
    CHECK(!effect_resolution::isCompositeBinding("_rt_Bloom"));
    CHECK(!effect_resolution::isCompositeBinding("previous"));
    return test::finish("effect resolution checks");
}
