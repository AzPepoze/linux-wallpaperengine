#include "pointer_input.h"

#include <cmath>

namespace {

constexpr float kHitEpsilon = 1e-4f;
constexpr float kMinDeterminant = 1e-12f;

PointerEvent makeEvent(PointerEventType type, uint8_t button, const LocalHit& hit) {
    PointerEvent event;
    event.type = type;
    event.button = button;
    event.id = hit.id;
    event.local_x = hit.local_x;
    event.local_y = hit.local_y;
    event.world_x = hit.world_x;
    event.world_y = hit.world_y;
    return event;
}

}  // namespace

WorldPoint screenToWorld(float sx, float sy, float offset_x, float offset_y, float render_scale, float scene_h) {
    WorldPoint point;
    if (render_scale == 0.0f) return point;
    point.x = (sx - offset_x) / render_scale;
    point.y = scene_h - (sy - offset_y) / render_scale;
    return point;
}

namespace {
std::optional<LocalHit> layerHit(WorldPoint cursor, const HitCandidate& candidate, bool clip) {
    if (!candidate.visible || !candidate.solid) return std::nullopt;
    const float* m = candidate.world;
    const float a = m[0], b = m[1], c = m[4], d = m[5];
    const float determinant = a * d - b * c;
    if (!std::isfinite(determinant) || std::fabs(determinant) < kMinDeterminant) return std::nullopt;
    const float px = cursor.x - candidate.offset[0] - m[12];
    const float py = cursor.y - candidate.offset[1] - m[13];
    const float local_x = (d * px - c * py) / determinant;
    const float local_y = (-b * px + a * py) / determinant;
    const float half_w = candidate.size[0] * 0.5f, half_h = candidate.size[1] * 0.5f;
    if (clip && (std::fabs(local_x) > half_w + kHitEpsilon || std::fabs(local_y) > half_h + kHitEpsilon))
        return std::nullopt;
    return LocalHit{candidate.id, local_x + half_w, half_h - local_y, cursor.x, cursor.y};
}
}  // namespace

std::optional<LocalHit> hitTest(WorldPoint cursor, const std::vector<HitCandidate>& drawn_in_order) {
    for (auto it = drawn_in_order.rbegin(); it != drawn_in_order.rend(); ++it)
        if (auto hit = layerHit(cursor, *it, true)) return hit;
    return std::nullopt;
}

std::vector<PointerEvent> PointerTracker::update(const std::optional<LocalHit>& hit, uint8_t buttons,
                                                 std::optional<WorldPoint> cursor,
                                                 const std::vector<HitCandidate>& candidates) {
    std::vector<PointerEvent> events;
    const bool has_hit = hit.has_value();
    const bool same_layer = has_hit && hovered_.has_value() && hit->id == hovered_->id;

    if (hovered_.has_value() && !same_layer) events.push_back(makeEvent(PointerEventType::Leave, 0, *hovered_));
    if (has_hit && !same_layer) events.push_back(makeEvent(PointerEventType::Enter, 0, *hit));
    const bool captured = pressed_[0].has_value();
    if (!captured && same_layer && (hit->world_x != hovered_->world_x || hit->world_y != hovered_->world_y))
        events.push_back(makeEvent(PointerEventType::Move, 0, *hit));

    for (int b = 0; b < 3; ++b) {
        if (!pressed_[b]) continue;
        LocalHit current = *pressed_[b];
        if (cursor) {
            current.world_x = cursor->x;
            current.world_y = cursor->y;
            for (const HitCandidate& candidate : candidates) {
                if (candidate.id != current.id) continue;
                if (auto local = layerHit(*cursor, candidate, false)) current = *local;
                break;
            }
        } else if (hit && hit->id == current.id)
            current = *hit;
        if (b == 0 && (current.world_x != pressed_[b]->world_x || current.world_y != pressed_[b]->world_y))
            events.push_back(makeEvent(PointerEventType::Move, 0, current));
        pressed_[b] = current;
    }

    if (has_hit)
        hovered_ = hit;
    else
        hovered_.reset();

    const uint8_t newly_pressed = buttons & static_cast<uint8_t>(~buttons_);
    const uint8_t released = buttons_ & static_cast<uint8_t>(~buttons);
    buttons_ = buttons;

    for (int b = 0; b < 3; ++b) {
        if ((newly_pressed & (1u << b)) && has_hit) {
            events.push_back(makeEvent(PointerEventType::Down, static_cast<uint8_t>(b), *hit));
            pressed_[b] = hit;
        }
    }
    for (int b = 0; b < 3; ++b) {
        if (!(released & (1u << b)) || !pressed_[b].has_value()) continue;
        const LocalHit target = *pressed_[b];
        events.push_back(makeEvent(PointerEventType::Up, static_cast<uint8_t>(b), target));
        if (has_hit && hit->id == pressed_[b]->id)
            events.push_back(makeEvent(PointerEventType::Click, static_cast<uint8_t>(b), *hit));
        pressed_[b].reset();
    }
    return events;
}
