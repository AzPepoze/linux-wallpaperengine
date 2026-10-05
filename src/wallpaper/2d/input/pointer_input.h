#ifndef POINTER_INPUT_H
#define POINTER_INPUT_H

#include <stdint.h>

#include <optional>
#include <vector>

// Pointer geometry and hover/button tracking, free of GPU includes so it is unit-testable.

struct WorldPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// A hit in a layer's local space: x/y run from the layer's top-left, y down,
// matching Wallpaper Engine's CursorEvent.localPosition.
struct LocalHit {
    uint32_t id = 0;
    float local_x = 0.0f;
    float local_y = 0.0f;
    float world_x = 0.0f;
    float world_y = 0.0f;
};

struct HitCandidate {
    uint32_t id = 0;
    // SceneTree world transform (mat4x4 flattened, column * 4 + row).
    float world[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    float size[2] = {0.0f, 0.0f};
    // Parallax shift in scene units on top of the world transform (not routed in yet, so zero).
    float offset[2] = {0.0f, 0.0f};
    bool solid = false;
    bool visible = true;
};

// Maps a screen pixel to scene-world coordinates. Screen y grows down, scene y
// grows up; offset is the screen position of the scene's top-left corner.
WorldPoint screenToWorld(float sx, float sy, float offset_x, float offset_y, float render_scale, float scene_h);

// Topmost (last drawn) visible solid candidate under the cursor, or nullopt.
std::optional<LocalHit> hitTest(WorldPoint cursor, const std::vector<HitCandidate>& drawn_in_order);

enum class PointerEventType { Leave, Enter, Move, Down, Up, Click };

struct PointerEvent {
    PointerEventType type = PointerEventType::Move;
    uint8_t button = 0;
    uint32_t id = 0;
    float local_x = 0.0f;
    float local_y = 0.0f;
    float world_x = 0.0f;
    float world_y = 0.0f;
};

class PointerTracker {
   public:
    // hit is the current layer under the cursor (if any), buttons is the live
    // press mask (bit0 left, bit1 right, bit2 middle).
    std::vector<PointerEvent> update(const std::optional<LocalHit>& hit, uint8_t buttons,
                                     std::optional<WorldPoint> cursor = std::nullopt,
                                     const std::vector<HitCandidate>& candidates = {});

   private:
    std::optional<LocalHit> hovered_;
    std::optional<LocalHit> pressed_[3];
    uint8_t buttons_ = 0;
};

#endif  // POINTER_INPUT_H
