#include "Editor/NodeGraph/HIKARI_NodeGraphCanvas.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {

void NodeGraphCanvas::ZoomAt(NodeGraphCanvasState& state, const MATH::Vec2& pointerScreen,
                             const MATH::Vec2& canvasOrigin, float wheelDelta, float minimumZoom,
                             float maximumZoom) noexcept {
    if (wheelDelta == 0.0f) {
        return;
    }
    const float oldZoom = state.zoom;
    const float newZoom = std::clamp(oldZoom * std::pow(1.12f, wheelDelta), minimumZoom, maximumZoom);
    const MATH::Vec2 world{(pointerScreen.x - canvasOrigin.x - state.pan.x) / oldZoom,
                           (pointerScreen.y - canvasOrigin.y - state.pan.y) / oldZoom};
    state.zoom = newZoom;
    state.pan = {pointerScreen.x - canvasOrigin.x - world.x * newZoom,
                 pointerScreen.y - canvasOrigin.y - world.y * newZoom};
}

void NodeGraphCanvas::Pan(NodeGraphCanvasState& state, const MATH::Vec2& screenDelta) noexcept {
    state.pan.x += screenDelta.x;
    state.pan.y += screenDelta.y;
}

MATH::Vec2 NodeGraphCanvas::CanvasToScreen(const NodeGraphCanvasState& state, const MATH::Vec2& canvasOrigin,
                                           const MATH::Vec2& canvasPosition) noexcept {
    return {canvasOrigin.x + state.pan.x + canvasPosition.x * state.zoom,
            canvasOrigin.y + state.pan.y + canvasPosition.y * state.zoom};
}

MATH::Vec2 NodeGraphCanvas::ScreenToCanvas(const NodeGraphCanvasState& state, const MATH::Vec2& canvasOrigin,
                                           const MATH::Vec2& screenPosition) noexcept {
    return {(screenPosition.x - canvasOrigin.x - state.pan.x) / state.zoom,
            (screenPosition.y - canvasOrigin.y - state.pan.y) / state.zoom};
}

void NodeGraphCanvas::MoveNode(const NodeGraphCanvasState& state, MATH::Vec2& nodePosition,
                               const MATH::Vec2& screenDelta) noexcept {
    nodePosition.x += screenDelta.x / state.zoom;
    nodePosition.y += screenDelta.y / state.zoom;
}

void NodeGraphCanvas::FrameBounds(NodeGraphCanvasState& state, const MATH::Vec2& viewportSize,
                                  const MATH::Vec2& minimum, const MATH::Vec2& maximum, float padding,
                                  float minimumZoom, float maximumZoom) noexcept {
    const float width = (std::max)(maximum.x - minimum.x, 1.0f);
    const float height = (std::max)(maximum.y - minimum.y, 1.0f);
    state.zoom = std::clamp((std::min)((viewportSize.x - padding) / width, (viewportSize.y - padding) / height),
                            minimumZoom, maximumZoom);
    state.pan = {(viewportSize.x - width * state.zoom) * 0.5f - minimum.x * state.zoom,
                 (viewportSize.y - height * state.zoom) * 0.5f - minimum.y * state.zoom};
}

bool NodeGraphCanvas::HitRect(const MATH::Vec2& pointer, const MATH::Vec2& minimum, const MATH::Vec2& maximum,
                              float padding) noexcept {
    return pointer.x >= minimum.x - padding && pointer.x <= maximum.x + padding && pointer.y >= minimum.y - padding &&
           pointer.y <= maximum.y + padding;
}

bool NodeGraphCanvas::HitPoint(const MATH::Vec2& pointer, const MATH::Vec2& target, float radius) noexcept {
    const float dx = pointer.x - target.x;
    const float dy = pointer.y - target.y;
    return dx * dx + dy * dy <= radius * radius;
}

bool NodeGraphCanvas::HitBezierCubic(const MATH::Vec2& pointer, const MATH::Vec2& start, const MATH::Vec2& controlA,
                                     const MATH::Vec2& controlB, const MATH::Vec2& end, float tolerance,
                                     uint32_t segmentCount) noexcept {
    if (tolerance < 0.0f || segmentCount == 0u) {
        return false;
    }

    const auto evaluate = [&](float t) noexcept {
        const float oneMinusT = 1.0f - t;
        const float a = oneMinusT * oneMinusT * oneMinusT;
        const float b = 3.0f * oneMinusT * oneMinusT * t;
        const float c = 3.0f * oneMinusT * t * t;
        const float d = t * t * t;
        return MATH::Vec2{start.x * a + controlA.x * b + controlB.x * c + end.x * d,
                          start.y * a + controlA.y * b + controlB.y * c + end.y * d};
    };
    const auto distanceToSegmentSquared = [](const MATH::Vec2& point, const MATH::Vec2& lineStart,
                                             const MATH::Vec2& lineEnd) noexcept {
        const float dx = lineEnd.x - lineStart.x;
        const float dy = lineEnd.y - lineStart.y;
        const float lengthSquared = dx * dx + dy * dy;
        float t = 0.0f;
        if (lengthSquared > 0.000001f) {
            t = std::clamp(((point.x - lineStart.x) * dx + (point.y - lineStart.y) * dy) / lengthSquared, 0.0f, 1.0f);
        }
        const float closestX = lineStart.x + dx * t;
        const float closestY = lineStart.y + dy * t;
        const float offsetX = point.x - closestX;
        const float offsetY = point.y - closestY;
        return offsetX * offsetX + offsetY * offsetY;
    };

    const float toleranceSquared = tolerance * tolerance;
    MATH::Vec2 previous = start;
    for (uint32_t segment = 1u; segment <= segmentCount; ++segment) {
        const MATH::Vec2 current = evaluate(static_cast<float>(segment) / static_cast<float>(segmentCount));
        if (distanceToSegmentSquared(pointer, previous, current) <= toleranceSquared) {
            return true;
        }
        previous = current;
    }
    return false;
}

} // namespace HIKARI::EDITOR
