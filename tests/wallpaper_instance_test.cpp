#include "wallpaper/wallpaper_instance.h"

#include "test_util.h"

// A stand-in EngineContext with only the per-wallpaper fields, avoiding the sokol graphics stack.
struct FakeContext {
    char asset_root[512] = {};
    char wallpaper_path[512] = {};
    bool is_pkg = false;
    scene_type_t scene_type = SCENE_TYPE_2D;
    UserProperties user_properties;
    SceneState scene;
    ParallaxState parallax;
    CameraShakeState shake;
};

int main() {
    FakeContext ctx;
    InstanceState a;
    InstanceState b;
    a.scene.scene_w = 100.0f;
    b.scene.scene_w = 200.0f;
    a.parallax.amount = 1.0f;
    b.parallax.amount = 2.0f;
    a.wallpaper_path = "/wp/a";

    activateInstanceState(ctx, a);
    CHECK(ctx.scene.scene_w == 100.0f);
    CHECK(ctx.parallax.amount == 1.0f);
    CHECK(std::string(ctx.wallpaper_path) == "/wp/a");

    // Mutating the active view must land back in the instance when it is stashed.
    ctx.scene.scene_w = 111.0f;
    stashInstanceState(ctx, a);
    CHECK(a.scene.scene_w == 111.0f);

    // Activating another state replaces the view wholesale.
    activateInstanceState(ctx, b);
    CHECK(ctx.scene.scene_w == 200.0f);
    CHECK(ctx.parallax.amount == 2.0f);

    stashInstanceState(ctx, b);
    CHECK(b.scene.scene_w == 200.0f);
    return test::finish("wallpaper instance checks");
}
