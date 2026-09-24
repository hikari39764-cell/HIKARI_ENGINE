#pragma once
#include "Render3D/ScreenSpace/HIKARI_ScreenReadability.h"

#include <cstdint>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderViewIdentity.h"

namespace HIKARI {
class AssetRegistry;
struct GameProjectRenderFrameView;
struct GameProjectRenderPassDesc;
struct GameProjectRenderPassContext;
} // namespace HIKARI

namespace HIKARI::RENDER3D {

namespace PARTICLES {
struct ParticleRenderFrameSource;
}

namespace WORLD_SPRITES {
struct WorldSpriteFrameSource;
}

namespace WORLD_TEXT {
struct WorldTextFrameSource;
}

namespace COMBAT_VFX {
struct CombatAfterimageFrameSource;
struct CombatPresentationFrameSource;
struct PerfectGuardShockwaveFrameSource;
} // namespace COMBAT_VFX
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

struct RenderFrameProjectExtensionSink {
    using ObserveFn = bool (*)(void* context, const GameProjectRenderFrameView& frame) noexcept;
    using GetPassCountFn = bool (*)(void* context, uint32_t& outCount) noexcept;
    using DescribePassFn = bool (*)(void* context, uint32_t index, GameProjectRenderPassDesc& outDescriptor) noexcept;
    using ExecutePassFn = bool (*)(void* context, const char* stableId, const GameProjectRenderPassContext& pass,
                                   bool& outCallbackEntered) noexcept;

    void* context = nullptr;
    ObserveFn observe = nullptr;
    GetPassCountFn getPassCount = nullptr;
    DescribePassFn describePass = nullptr;
    ExecutePassFn executePass = nullptr;
    uint64_t worldIdentity = 0u;

    bool CanObserve() const noexcept { return context != nullptr && observe != nullptr && worldIdentity != 0u; }
    bool CanExecutePasses() const noexcept {
        return context != nullptr && getPassCount != nullptr && describePass != nullptr && executePass != nullptr &&
               worldIdentity != 0u;
    }
};

struct RenderViewContext {
    ScreenReadabilityWorldFrame screenReadability{};
    RenderViewId viewId{};
    RenderViewPurpose purpose = RenderViewPurpose::Game;
    const ResolvedCameraFrame* cameraFrame = nullptr;
    const PARTICLES::ParticleRenderFrameSource* particleFrameSource = nullptr;
    const WORLD_SPRITES::WorldSpriteFrameSource* worldSpriteFrameSource = nullptr;
    const WORLD_TEXT::WorldTextFrameSource* worldTextFrameSource = nullptr;
    const COMBAT_VFX::CombatPresentationFrameSource* combatPresentationFrameSource = nullptr;
    const COMBAT_VFX::CombatAfterimageFrameSource* combatAfterimageFrameSource = nullptr;
    const COMBAT_VFX::PerfectGuardShockwaveFrameSource* actionPostReactionFrameSource = nullptr;
    const AssetRegistry* assetRegistry = nullptr;
    RenderFrameProjectExtensionSink projectRenderExtensions{};
};

} // namespace HIKARI::RENDER3D
