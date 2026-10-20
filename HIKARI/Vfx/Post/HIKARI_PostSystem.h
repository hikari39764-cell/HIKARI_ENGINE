#pragma once
#include <span>
#include "Scene/Sequencer/Tracks/HIKARI_ShotPresentation.h"

#include <span>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"

#include <DirectXMath.h>

#include "Assets/Post/HIKARI_PostAsset.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Color/HIKARI_AutomaticExposure.h"
#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameGraphSession.h"
#include "Render3D/Surface/HIKARI_RenderFrameSurfaceRegistry.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Vfx/HIKARI_ActionPostReactionFrameSource.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_ActionScreenImpulseProvider.h"
#include "Vfx/Post/HIKARI_PostProcessingTypes.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Vfx/Post/HIKARI_PresentationFrame.h"
#include "Vfx/Post/HIKARI_SceneColorSnapshot.h"

namespace HIKARI {
class Camera3D;
class RenderTarget2D;
} // namespace HIKARI

namespace HIKARI::ASSETS::COLOR {
struct ColorLutAsset;
}

namespace HIKARI::CAMERA {
struct DepthOfFieldSettings;
struct MotionBlurSettings;
} // namespace HIKARI::CAMERA

namespace HIKARI::RENDER3D {
enum class DepthOfFieldQuality : uint8_t;
}

namespace HIKARI::POST {

class PostEffect;
class GameUiCompositionStage;
class PostPresentationStage;
class PostProcessingStage;
class SceneCaptureStage;
class PreservedVfxLayer;

// Compatibility facade. Resource ownership lives in the three stages below.
class PostSystem {
public:
    using BloomDebugStats = POST::BloomDebugStats;
    using FxaaSettings = POST::FxaaSettings;
    using SunGlareFrame = POST::SunGlareFrame;
    using SunGlareDebugStats = POST::SunGlareDebugStats;
    using ActionScreenImpulseDebugState = POST::ActionScreenImpulseProvider::DebugState;
    using ActionScreenDistortionMask = POST::ActionScreenImpulseProvider::Mask;
    using ActionScreenDistortionAccent = POST::ActionScreenImpulseProvider::Mask;

    static void Initialize(const GFX::Context& context);
    static void UpdateContext(const GFX::Context& context);
    static void Shutdown();

    static void UpdateCommonParams(float deltaTime);
    // 主ビューの露出をこのフレーム分進めて P を固定する。色調設定の後、HDR へ書く前に一度だけ呼ぶ。
    // illumination は場面を照らす光の量（SKYRENDERER::EstimateExposureIllumination）。meterScene はこのフレームに主ビューが
    // 場面を描くか（編集器で主ビューを隠した工作区では描かれない画面を測らず、測光は前の値か入射照度のまま）。
    static void AdvanceExposure(float deltaTime, const RENDER3D::COLOR::ExposureIllumination& illumination,
                                bool meterScene);
    static void SetIntensity(float intensity);
    static void SetSequenceEffects(std::span<const SEQUENCER::ScreenEffectSample> effects);
    static void SetCombo(float combo);
    // 全体後処理の層（描画の様式の後処理）を並びの順に重ねる。空なら全体後処理を止める。
    static bool SetGlobalEffects(std::span<const ResolvedPostLayer> layers, const AssetRegistry* registry = nullptr);
    static bool RequiresGlobalSurfaceFeatureMask() noexcept;
    static bool SetActionPostFrame(uint64_t worldId, uint64_t frameIndex, std::string_view effectId,
                                   std::shared_ptr<const PostEffectDefinition> effect, const PostParameterBlock& values,
                                   const AssetRegistry& registry);
    static void ClearActionPostFrame(uint64_t worldId = 0u);
    static void SetActionPostView(uint64_t worldId, bool primaryGameView) noexcept;
    static bool RequiresActionSurfaceFeatureMask() noexcept;
    // 実行中の全体効果のパラメーターを 1 フレームだけ上書きする。effectId の層が無ければ無視する（同じ効果の層すべてへ）。
    static bool SubmitEffectParameter(std::string_view effectId, uint32_t index, uint32_t channelMask,
                                      const DirectX::XMFLOAT4& value, int32_t priority, uint64_t sourceId);
    static bool GetRadialScreenWarp(float& curvature, float& strength) noexcept;
    static void SetBloomSettings(const BloomSettings& settings);
    static void SetScreenReadability(const RENDER3D::ScreenReadabilityFrame& frame);
    // 場景キャプチャを映す面。時間方向の解決（TAA・DLSS）がその画素で履歴を弱める。
    static void SetSceneCaptureSurfaces(const RENDER3D::ScreenReadabilityFrame& frame);
    static void SetToneMappingSettings(const ToneMappingSettings& settings);
    // 表示するカメラの運動ブラー。TAA の後、HDR の後処理の前に掛ける。
    static void SetCameraMotionBlur(const CAMERA::MotionBlurSettings& settings, bool orthographic);
    static void SetColorGrading(const ColorGradingSettings& settings,
                                std::shared_ptr<const ASSETS::COLOR::ColorLutAsset> lut);
    // 主ビューが最後に焼いた色調整の LUT（同じ環境を描く別のビューが借りる）。調整が無ければ nullptr。
    // 主ビューを描かないフレームでは前回焼いたもののまま。
    static ID3D12Resource* GetColorGradingLut() noexcept;
    static void SetExposureSettings(const RENDER3D::COLOR::ExposureSettings& settings);
    static void SetFxaaSettings(const FxaaSettings& settings);
    static void SetSunGlareFrame(const SunGlareFrame& frame);
    static void ClearSunGlareFrame();
    // Copies the current perfect-guard source and render camera for the
    // post-temporal HDR impulse. No scene/service pointer is retained.
    static void SetActionScreenImpulseFrame(const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource& source,
                                            const MATH::Mat4& viewProj, uint64_t frameIndex);
    static void SetActionScreenDistortionMask(const ActionScreenDistortionMask& mask);
    static void SetActionScreenDistortionAccent(const ActionScreenDistortionAccent& accent);
    static const FxaaSettings& GetFxaaSettings();
    static const BloomDebugStats& GetBloomDebugStats();
    static const SunGlareDebugStats& GetSunGlareDebugStats();
    static const ActionScreenImpulseDebugState& GetActionScreenImpulseDebugState();
    static const RENDER3D::COLOR::ExposureFrameState& GetExposureState();
    static const RENDER3D::COLOR::AutomaticExposureDebugState& GetAutomaticExposureDebugState();
    static void SetTransitionState(const TransitionVisualState& state);
    static void ClearTransitionState();

    static std::string DumpFrameState();
    static void LogFrameState(const char* reason);
    static void RequestFrameDump();

    static bool BeginSceneCapture(RENDER3D::RenderFrameToken frame, uint64_t deviceGeneration);
    static RENDER3D::SURFACE::RenderFrameSurfaceReceipt GetSceneCaptureSurfaceReceipt() noexcept;
    static bool SetSceneCaptureGraphSession(RENDER3D::PIPELINE::RenderFrameGraphSessionReceipt session);
    static bool ContinueSceneCaptureGraphSession();
    static bool SetSceneCaptureTemporalFrame(RENDER3D::TEMPORAL::TemporalViewLease view,
                                             RENDER3D::RenderFrameToken frame,
                                             RENDER3D::TEMPORAL::TemporalResourceFrame resources);
    static bool AbortSceneCaptureTemporalFrame(RENDER3D::TEMPORAL::TemporalHistoryResetReason reason);
    static bool TryGetPresentationTemporalInputs(RENDER3D::TEMPORAL::TemporalInputs& outInputs);
    static bool FinishPresentationTemporalFrame(bool succeeded);
    static bool HasCurrentRenderTarget();
    static bool RebindCurrentRenderTarget();
    static bool BindAdditionalColors(std::span<const D3D12_CPU_DESCRIPTOR_HANDLE> colors);
    static bool ClearCurrentRenderTargetColor(float red, float green, float blue, float alpha);
    static bool CopyToCurrentRenderTargetColor(ID3D12Resource* source, DXGI_FORMAT format, uint32_t width,
                                               uint32_t height);
    // Restores the full presentation back buffer for Editor host UI after
    // a tool-owned offscreen render. Unlike RebindCurrentRenderTarget,
    // this does not require an active Scene capture layer.
    static bool RebindPresentationBackBuffer();
    static RENDER3D::RenderTextureView GetCurrentRenderTargetDepthView();
    static RENDER3D::RenderTextureView GetCurrentRenderTargetColorView();
    static D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetDsv();
    static D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetReadOnlyDsv();
    static bool BeginPreservedVfxCapture(const RENDER3D::PIPELINE::RenderFrameContext& frame,
                                         const RENDER3D::TEMPORAL::TemporalFrameState& temporal,
                                         RENDER3D::PIPELINE::RenderTargetAccess& target);
    static bool EndPreservedVfxCapture(bool succeeded);
    static bool BeginCurrentRenderTargetDepthRead();
    static void EndCurrentRenderTargetDepthRead();
    static bool PrepareCameraDepthOfField(uint64_t frameIndex, const CAMERA::DepthOfFieldSettings& settings,
                                          RENDER3D::DepthOfFieldQuality quality, bool captureWorldSprites);
    static bool ExecuteCameraDepthOfField(uint64_t frameIndex,
                                          const RENDER3D::TEMPORAL::TemporalFrameState& temporalFrame,
                                          const Camera3D& camera, const CAMERA::DepthOfFieldSettings& settings,
                                          RENDER3D::DepthOfFieldQuality quality);
    static bool PrepareGameUiComposition(bool editorViewport, int uiLogicalWidth = 0, int uiLogicalHeight = 0);
    static bool FinalizeGameUiComposition(bool editorViewport);
    static const PresentationFrameResources& GetPresentationFrameResources();
    static void SetSceneCaptureSize(int renderWidth, int renderHeight, int outputWidth, int outputHeight);
    static void GetSceneCaptureSize(int& width, int& height);
    static void GetSceneOutputSize(int& width, int& height);

    static bool IsEditorViewportReady();
    static D3D12_GPU_DESCRIPTOR_HANDLE GetEditorViewportSrv();
    // 布景の A/B 分屏の A（次の編集器の画の複製）。要求の次のフレームから GetEditorSnapshotSrv が返す。
    static void RequestEditorSnapshot();
    static void ClearEditorSnapshot();
    static D3D12_GPU_DESCRIPTOR_HANDLE GetEditorSnapshotSrv();

    static RENDER3D::RenderTextureView CaptureSceneColorSnapshot(SceneColorSnapshotKind kind);
    static RENDER3D::RenderTextureView GetSceneColorSnapshotView(SceneColorSnapshotKind kind);

    static void SetAmbientColor(float r, float g, float b);
    static void BeginLightCapture();
    static void EndLightCapture();

private:
    static RenderTarget2D* EndSceneCaptureAndResolveFinal();
    static RenderTarget2D* ResolveFinalSceneToLdr(RenderTarget2D& source, DXGI_FORMAT outputFormat);

    static bool initialized_;
    static GFX::Context context_;
    static QuadDrawer quad_;
    static SceneCaptureStage captureStage_;
    static PreservedVfxLayer preservedVfxLayer_;
    static PostProcessingStage processingStage_;
    static GameUiCompositionStage gameUiCompositionStage_;
    static PostPresentationStage presentationStage_;
    static int requestedRenderWidth_;
    static int requestedRenderHeight_;
    static int requestedOutputWidth_;
    static int requestedOutputHeight_;
    static RENDER3D::TEMPORAL::TemporalViewLease temporalView_;
    static RENDER3D::RenderFrameToken temporalFrame_;
    static RENDER3D::TEMPORAL::TemporalResourceFrame temporalResources_;
    static RENDER3D::TEMPORAL::TemporalResourceFrame presentationTemporalResources_;
    static CAMERA::MotionBlurSettings cameraMotionBlur_;
    static bool cameraMotionBlurOrthographic_;
    static RENDER3D::ScreenReadabilityFrame sceneCaptureSurfaces_;
    static RENDER3D::SURFACE::RenderFrameSurfaceReceipt renderSurfaceReceipt_;
    static RENDER3D::PIPELINE::RenderFrameGraphSessionReceipt renderGraphSessionReceipt_;
};

} // namespace HIKARI::POST
