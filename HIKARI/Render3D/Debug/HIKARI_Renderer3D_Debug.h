#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Render2D/HIKARI_Transform2D.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Render3D/Core/HIKARI_RenderView.h"
#endif
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::RENDERER3D::DEBUG {

    // Editor overlay 3D debug primitive.
    enum class DebugDepthMode {
        DepthTest,
        XRay
    };

    struct Box3D {
        Transform3D transform{};
        MATH::Vec3 size{ 1.0f, 1.0f, 1.0f };
        unsigned int outlineRgba = 0xFFFFFFFF;
        unsigned int fillRgba = 0x00000000;
        float outlineThicknessPixels = 1.5f;
        DebugDepthMode depthMode = DebugDepthMode::DepthTest;
    };

    struct Stroke3D {
        MATH::Vec3 from{};
        MATH::Vec3 to{};
        unsigned int rgba = 0xFFFFFFFF;
        DebugDepthMode depthMode = DebugDepthMode::DepthTest;
        float thicknessPixels = 1.5f;
    };

    struct PointMarker3D {
        MATH::Vec3 position{};
        float radius = 0.08f;
        unsigned int rgba = 0xFFFFFFFF;
        DebugDepthMode depthMode = DebugDepthMode::DepthTest;
        float thicknessPixels = 1.5f;
    };

    struct Axis3D {
        Transform3D transform{};
        float length = 1.0f;
        unsigned int xColor = 0xFF4C4CFF;
        unsigned int yColor = 0x4CFF4CFF;
        unsigned int zColor = 0x4C4CFFFF;
        DebugDepthMode depthMode = DebugDepthMode::DepthTest;
        float thicknessPixels = 1.5f;
    };

    struct Grid3D {
        int halfCount = 10;
        float spacing = 1.0f;
        unsigned int rgba = 0x888888FF;
        DebugDepthMode depthMode = DebugDepthMode::DepthTest;
        float thicknessPixels = 1.0f;
    };

    struct DebugRendererFrameStats {
        size_t submittedStrokeCount = 0;
        size_t submittedXRayStrokeCount = 0;
        size_t expandedStrokeCount = 0;
        size_t depthTestStrokeCount = 0;
        size_t xrayStrokeCount = 0;
        size_t boxCount = 0;
        size_t pointMarkerCount = 0;
        size_t solidTriangleCount = 0;
        size_t axisCount = 0;
        size_t gridCount = 0;
        uint32_t lightProbeGizmoTotalPointCount = 0;
        uint32_t lightProbeGizmoDrawnPointCount = 0;
        uint32_t lightProbeGizmoMode = 0;
        bool lightProbeGizmoCapped = false;
    };

    void Reset();
    void SubmitBox3D(const Box3D& box);
    void SubmitStroke3D(const Stroke3D& stroke);
    void SubmitPointMarker3D(const PointMarker3D& marker);
    void SubmitAxis3D(const Axis3D& axis);
    void SubmitGrid3D(const Grid3D& grid);
    void AccumulateLightProbeVolumeGizmoStats(
        uint32_t totalPointCount,
        uint32_t drawnPointCount,
        uint32_t mode,
        bool capped);
    const DebugRendererFrameStats& GetDebugRendererFrameStats();
    void RenderAll(const Camera3D& camera, float screenW, float screenH);
#if defined(HIKARI_WITH_EDITOR)
    void RenderAllForView(
        RENDER3D::RenderViewId viewId,
        const Camera3D& camera,
        float screenW,
        float screenH);
    void ShutdownEditorViewResources();
#endif

} // namespace HIKARI::RENDERER3D::DEBUG
