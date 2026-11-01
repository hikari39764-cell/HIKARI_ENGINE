#pragma once
#include "Render3D/ScreenSpace/HIKARI_ScreenComposite.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenReadability.h"

#include <cstdint>
#include <span>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderViewIdentity.h"

namespace HIKARI {
class AssetRegistry;
struct GameProjectRenderFrameView;
struct GameProjectRenderPassDesc;
struct GameProjectRenderPassContext;
} // namespace HIKARI

namespace HIKARI::RENDER3D {

namespace VOLUMETRIC {
struct LocalFogVolumeGpu;
}

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
    // Director が連続性を確認した所有者交代。既定では別カメラの履歴を流用しない。
    bool continuousCameraSource = false;
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
    std::span<const VOLUMETRIC::LocalFogVolumeGpu> localFogVolumes{};
    ScreenReadabilityWorldFrame screenReadability{};
    // 場景キャプチャを映す面すべて。中身が面と別に動くので、時間方向の解決で履歴を弱める。
    ScreenReadabilityWorldFrame sceneCaptureSurfaces{};
    // 時間方向の拡大の後に出力の解像度で貼り直す画面（無ければ貼り直さない）。呼び出し側が描画の間持つ。
    const ScreenCompositeWorldFrame* screenComposites = nullptr;
    RenderViewId viewId{};
    RenderViewPurpose purpose = RenderViewPurpose::Game;
    const ResolvedCameraFrame* cameraFrame = nullptr;
    const PARTICLES::ParticleRenderFrameSource* particleFrameSource = nullptr;
    const WORLD_SPRITES::WorldSpriteFrameSource* worldSpriteFrameSource = nullptr;
    const WORLD_TEXT::WorldTextFrameSource* worldTextFrameSource = nullptr;
    const COMBAT_VFX::CombatPresentationFrameSource* combatPresentationFrameSource = nullptr;
    const COMBAT_VFX::PerfectGuardShockwaveFrameSource* actionPostReactionFrameSource = nullptr;
    const AssetRegistry* assetRegistry = nullptr;
    RenderFrameProjectExtensionSink projectRenderExtensions{};
};

} // namespace HIKARI::RENDER3D
