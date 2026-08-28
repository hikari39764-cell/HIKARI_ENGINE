#pragma once

#include <cstdint>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderViewIdentity.h"

namespace HIKARI::RENDER3D {

    namespace PARTICLES {
        struct ParticleRenderFrameSource;
    }

    namespace COMBAT_VFX {
        struct CombatAfterimageFrameSource;
        struct CombatPresentationFrameSource;
        struct PerfectGuardShockwaveFrameSource;
    }
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
        const PARTICLES::ParticleRenderFrameSource* particleFrameSource =
            nullptr;
        const COMBAT_VFX::CombatPresentationFrameSource*
            combatPresentationFrameSource = nullptr;
        const COMBAT_VFX::CombatAfterimageFrameSource*
            combatAfterimageFrameSource = nullptr;
        const COMBAT_VFX::PerfectGuardShockwaveFrameSource*
            actionPostReactionFrameSource = nullptr;
    };

} // namespace HIKARI::RENDER3D
