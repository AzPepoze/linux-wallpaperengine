#include "wallpaper/2d/input/pointer_input.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "test_util.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

void identity(float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// Column-major flattened mat4x4, same layout SceneTree::worldTransform writes.
void trs(float m[16], float tx, float ty, float sx, float sy, float degrees) {
    identity(m);
    const float r = degrees * kPi / 180.0f;
    const float c = std::cos(r);
    const float s = std::sin(r);
    m[0] = c * sx;
    m[1] = s * sx;
    m[4] = -s * sy;
    m[5] = c * sy;
    m[12] = tx;
    m[13] = ty;
}

HitCandidate candidate(uint32_t id, const float world[16], float w, float h, bool solid, bool visible) {
    HitCandidate c;
    c.id = id;
    for (int i = 0; i < 16; ++i) c.world[i] = world[i];
    c.size[0] = w;
    c.size[1] = h;
    c.solid = solid;
    c.visible = visible;
    return c;
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-3f;
}

void checkScreenToWorld() {
    const WorldPoint w = screenToWorld(300.0f, 250.0f, 100.0f, 50.0f, 2.0f, 100.0f);
    CHECK(near(w.x, 100.0f));
    CHECK(near(w.y, 0.0f));

    // The scene's top-left corner maps back to world (0, scene_h).
    const WorldPoint top_left = screenToWorld(100.0f, 50.0f, 100.0f, 50.0f, 2.0f, 100.0f);
    CHECK(near(top_left.x, 0.0f));
    CHECK(near(top_left.y, 100.0f));

    // Round trip: world -> screen -> world.
    const float sx = 100.0f + 42.0f * 2.0f;
    const float sy = 50.0f + (100.0f - 17.0f) * 2.0f;
    const WorldPoint back = screenToWorld(sx, sy, 100.0f, 50.0f, 2.0f, 100.0f);
    CHECK(near(back.x, 42.0f));
    CHECK(near(back.y, 17.0f));
}

void checkHitRotated() {
    float m[16];
    trs(m, 0.0f, 0.0f, 1.0f, 1.0f, 45.0f);
    const std::vector<HitCandidate> layers = {candidate(1, m, 100.0f, 100.0f, true, true)};

    const auto center = hitTest({0.0f, 0.0f}, layers);
    CHECK(center.has_value());
    if (center) {
        CHECK(center->id == 1);
        CHECK(near(center->local_x, 50.0f));
        CHECK(near(center->local_y, 50.0f));
        CHECK(near(center->world_x, 0.0f));
        CHECK(near(center->world_y, 0.0f));
    }

    CHECK(hitTest({0.0f, 60.0f}, layers).has_value());
    CHECK(!hitTest({0.0f, 80.0f}, layers).has_value());
    // (-60, 0) rotates back to local (-42.43, 42.43), so from the top-left it is (7.57, 7.57).
    const auto side = hitTest({-60.0f, 0.0f}, layers);
    CHECK(side.has_value());
    if (side) {
        CHECK(near(side->local_x, 7.574f));
        CHECK(near(side->local_y, 7.574f));
    }
}

void checkNonUniformScale() {
    float m[16];
    trs(m, 0.0f, 0.0f, 2.0f, 1.0f, 0.0f);
    const std::vector<HitCandidate> layers = {candidate(7, m, 100.0f, 50.0f, true, true)};

    const auto hit = hitTest({90.0f, 20.0f}, layers);
    CHECK(hit.has_value());
    if (hit) {
        CHECK(near(hit->local_x, 95.0f));
        CHECK(near(hit->local_y, 5.0f));
    }
    CHECK(!hitTest({110.0f, 0.0f}, layers).has_value());
}

void checkNegativeScale() {
    float m[16];
    trs(m, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f);
    const std::vector<HitCandidate> layers = {candidate(3, m, 100.0f, 50.0f, true, true)};

    const auto hit = hitTest({-40.0f, 10.0f}, layers);
    CHECK(hit.has_value());
    if (hit) {
        CHECK(near(hit->local_x, 90.0f));
        CHECK(near(hit->local_y, 15.0f));
    }
}

void checkParentTransform() {
    // Composed parent (100,0) * child (50,0) translation.
    float m[16];
    trs(m, 150.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    const std::vector<HitCandidate> layers = {candidate(9, m, 40.0f, 40.0f, true, true)};
    CHECK(!hitTest({0.0f, 0.0f}, layers).has_value());
    CHECK(hitTest({150.0f, 0.0f}, layers).has_value());
}

void checkTopmostAndFiltering() {
    float back[16];
    float front[16];
    trs(back, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    trs(front, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    const HitCandidate a = candidate(11, back, 100.0f, 100.0f, true, true);
    const HitCandidate b = candidate(22, front, 100.0f, 100.0f, true, true);

    const auto top = hitTest({0.0f, 0.0f}, {a, b});
    CHECK(top.has_value() && top->id == 22);
    const auto flipped = hitTest({0.0f, 0.0f}, {b, a});
    CHECK(flipped.has_value() && flipped->id == 11);

    const HitCandidate invisible = candidate(33, front, 100.0f, 100.0f, true, false);
    const HitCandidate non_solid = candidate(44, front, 100.0f, 100.0f, false, true);
    CHECK(!hitTest({0.0f, 0.0f}, {invisible}).has_value());
    CHECK(!hitTest({0.0f, 0.0f}, {non_solid}).has_value());
    // A visible solid layer below a non-solid one still wins.
    const auto below = hitTest({0.0f, 0.0f}, {a, non_solid});
    CHECK(below.has_value() && below->id == 11);
}

std::optional<LocalHit> makeHit(uint32_t id, float lx, float ly) {
    LocalHit h;
    h.id = id;
    h.local_x = lx;
    h.local_y = ly;
    h.world_x = lx;
    h.world_y = ly;
    return h;
}

bool has(const std::vector<PointerEvent>& events, PointerEventType type, uint32_t id) {
    for (const PointerEvent& e : events) {
        if (e.type == type && e.id == id) return true;
    }
    return false;
}

void checkTrackerClick() {
    PointerTracker tracker;
    auto events = tracker.update(makeHit(1, 10.0f, 10.0f), 0);
    CHECK(events.size() == 1 && events[0].type == PointerEventType::Enter && events[0].id == 1);

    events = tracker.update(makeHit(1, 20.0f, 20.0f), 0);
    CHECK(events.size() == 1 && events[0].type == PointerEventType::Move);

    events = tracker.update(makeHit(1, 20.0f, 20.0f), 0x1);
    CHECK(events.size() == 1 && events[0].type == PointerEventType::Down && events[0].button == 0);

    events = tracker.update(makeHit(1, 20.0f, 20.0f), 0);
    CHECK(events.size() == 2);
    CHECK(events[0].type == PointerEventType::Up);
    CHECK(events[1].type == PointerEventType::Click);
    CHECK(events[1].button == 0 && events[1].id == 1);
}

void checkTrackerNoClickElsewhere() {
    PointerTracker tracker;
    tracker.update(makeHit(1, 0.0f, 0.0f), 0);
    tracker.update(makeHit(1, 0.0f, 0.0f), 0x1);

    const auto events = tracker.update(makeHit(2, 0.0f, 0.0f), 0);
    CHECK(!has(events, PointerEventType::Click, 1));
    CHECK(!has(events, PointerEventType::Click, 2));
    CHECK(has(events, PointerEventType::Leave, 1));
    CHECK(has(events, PointerEventType::Enter, 2));
    // Up targets the layer under the release so Down/Up stay paired; Click does not fire.
    CHECK(has(events, PointerEventType::Up, 2));
}

void checkTrackerLeaveEnterOrder() {
    PointerTracker tracker;
    auto events = tracker.update(makeHit(1, 0.0f, 0.0f), 0);
    CHECK(events.size() == 1 && events[0].type == PointerEventType::Enter && events[0].id == 1);

    events = tracker.update(makeHit(2, 0.0f, 0.0f), 0);
    CHECK(events.size() == 2);
    CHECK(events[0].type == PointerEventType::Leave && events[0].id == 1);
    CHECK(events[1].type == PointerEventType::Enter && events[1].id == 2);

    events = tracker.update(std::nullopt, 0);
    CHECK(events.size() == 1 && events[0].type == PointerEventType::Leave && events[0].id == 2);
}

}  // namespace

int main() {
    checkScreenToWorld();
    checkHitRotated();
    checkNonUniformScale();
    checkNegativeScale();
    checkParentTransform();
    checkTopmostAndFiltering();
    checkTrackerClick();
    checkTrackerNoClickElsewhere();
    checkTrackerLeaveEnterOrder();
    return test::finish("pointer input checks");
}
