#pragma once

#include "Render3D/Debug/HIKARI_RenderDebugView.h"

namespace HIKARI {

    enum class LightProbeVolumeOverlayMode {
        Off,
        BoundsOnly,
        SampledPoints,
        AllPoints,
    };

    enum class ReflectionProbeEditTarget {
        ProbePosition,
        InfluenceBox,
        ProjectionBox,
    };

    struct ViewportOverlayState {
        bool showGrid = true;
        bool showAxis = true;
        bool showLights = true;
        bool showReflectionProbe = true;
        bool showLightProbeVolume = false;
        bool showProbeLabels = true;
        bool showXRayGizmos = false;
        bool editReflectionProbe = false;
        LightProbeVolumeOverlayMode lightProbeVolumeMode =
            LightProbeVolumeOverlayMode::BoundsOnly;
        ReflectionProbeEditTarget reflectionProbeEditTarget =
            ReflectionProbeEditTarget::ProbePosition;
    };

    struct ViewportPerformanceState {
        bool disableSsaoInEditorViewport = false;
    };

    struct ViewportDebugViewState {
        RenderDebugView renderView = RenderDebugView::None;
        bool showLegend = true;
        bool freezeCullingCamera = false;
    };

} // namespace HIKARI
