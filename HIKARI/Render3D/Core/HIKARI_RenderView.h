#pragma once

#include <cstdint>

#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI::RENDER3D {

    struct RenderViewId {
        uint64_t value = 0;

        bool operator==(const RenderViewId& rhs) const noexcept {
            return value == rhs.value;
        }
    };

    enum class RenderViewPurpose : uint8_t {
        Game,
        EditorScene,
        ToolPreview,
        ReflectionProbeCapture,
        LightProbeCapture,
    };

    struct ResolvedCameraFrame {
        Camera3D camera{};
        uint64_t sourceCameraObjectId = 0;
        uint64_t revision = 0;
        bool cameraCut = false;
        bool projectionChanged = false;
        bool valid = false;
    };

    struct RenderViewContext {
        RenderViewId viewId{};
        RenderViewPurpose purpose = RenderViewPurpose::Game;
        const ResolvedCameraFrame* cameraFrame = nullptr;
    };

    inline constexpr RenderViewId kPrimaryRenderViewId{ 1 };

} // namespace HIKARI::RENDER3D
