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

    struct WireCube {
        Transform3D transform{};
        float size = 1.0f;
        unsigned int rgba = 0xFFFFFFFF;
        DebugDepthMode depthMode = DebugDepthMode::DepthTest;
    };

    struct Line3D {
        MATH::Vec3 from{};
        MATH::Vec3 to{};
        unsigned int rgba = 0xFFFFFFFF;
        DebugDepthMode depthMode = DebugDepthMode::DepthTest;
    };

    struct Axis3D {
        Transform3D transform{};
        float length = 1.0f;
        unsigned int xColor = 0xFF4C4CFF;
        unsigned int yColor = 0x4CFF4CFF;
        unsigned int zColor = 0x4C4CFFFF;
        DebugDepthMode depthMode = DebugDepthMode::DepthTest;
    };

    struct Grid3D {
        int halfCount = 10;
        float spacing = 1.0f;
        unsigned int rgba = 0x888888FF;
        DebugDepthMode depthMode = DebugDepthMode::DepthTest;
    };

    struct DebugRendererFrameStats {
        size_t submittedLineCount = 0;
        size_t submittedXRayLineCount = 0;
        size_t expandedLineCount = 0;
        size_t depthTestLineCount = 0;
        size_t xrayLineCount = 0;
        size_t wireCubeCount = 0;
        size_t axisCount = 0;
        size_t gridCount = 0;
        uint32_t lightProbeGizmoTotalPointCount = 0;
        uint32_t lightProbeGizmoDrawnPointCount = 0;
        uint32_t lightProbeGizmoMode = 0;
        bool lightProbeGizmoCapped = false;
    };

    void Reset();
    void SubmitWireCube(const WireCube& cube);
    void SubmitLine3D(const Line3D& line);
    void SubmitAxis3D(const Axis3D& axis);
    void SubmitGrid3D(const Grid3D& grid);
    void SetLightProbeVolumeGizmoStats(
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
