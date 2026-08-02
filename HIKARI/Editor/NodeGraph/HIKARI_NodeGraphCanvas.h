#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::EDITOR {

    struct NodeGraphCanvasState {
        MATH::Vec2 pan{ 20.0f, 20.0f };
        float zoom = 1.0f;
    };

    // Shared coordinate and interaction policy for Editor node canvases.
    // Domain editors own their nodes, pins and commands; this layer owns the
    // canvas navigation math and node placement behavior.
    class NodeGraphCanvas final {
    public:
        static void ZoomAt(
            NodeGraphCanvasState& state,
            const MATH::Vec2& pointerScreen,
            const MATH::Vec2& canvasOrigin,
            float wheelDelta,
            float minimumZoom = 0.35f,
            float maximumZoom = 2.5f) noexcept;
        static void Pan(
            NodeGraphCanvasState& state,
            const MATH::Vec2& screenDelta) noexcept;
        static MATH::Vec2 CanvasToScreen(
            const NodeGraphCanvasState& state,
            const MATH::Vec2& canvasOrigin,
            const MATH::Vec2& canvasPosition) noexcept;
        static MATH::Vec2 ScreenToCanvas(
            const NodeGraphCanvasState& state,
            const MATH::Vec2& canvasOrigin,
            const MATH::Vec2& screenPosition) noexcept;
        static void MoveNode(
            const NodeGraphCanvasState& state,
            MATH::Vec2& nodePosition,
            const MATH::Vec2& screenDelta) noexcept;
        static void FrameBounds(
            NodeGraphCanvasState& state,
            const MATH::Vec2& viewportSize,
            const MATH::Vec2& minimum,
            const MATH::Vec2& maximum,
            float padding = 80.0f,
            float minimumZoom = 0.35f,
            float maximumZoom = 2.5f) noexcept;

        static bool HitRect(
            const MATH::Vec2& pointer,
            const MATH::Vec2& minimum,
            const MATH::Vec2& maximum,
            float padding = 0.0f) noexcept;

        static bool HitPoint(
            const MATH::Vec2& pointer,
            const MATH::Vec2& target,
            float radius) noexcept;

        static bool HitBezierCubic(
            const MATH::Vec2& pointer,
            const MATH::Vec2& start,
            const MATH::Vec2& controlA,
            const MATH::Vec2& controlB,
            const MATH::Vec2& end,
            float tolerance,
            uint32_t segmentCount = 24u) noexcept;
    };

} // namespace HIKARI::EDITOR
