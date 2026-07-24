#pragma once

#include <string>

#include <DirectXMath.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_PostProcessingTypes.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Vfx/Post/HIKARI_PresentationFrame.h"

namespace HIKARI {
    class RenderTarget2D;
}

namespace HIKARI::POST {

    class PostEffect;
    class PostChain;
    class GameUiCompositionStage;
    class PostPresentationStage;
    class PostProcessingStage;
    class SceneCaptureStage;

    // Compatibility facade. Resource ownership lives in the three stages below.
    class PostSystem {
    public:
        using BloomDebugStats = POST::BloomDebugStats;
        using FxaaSettings = POST::FxaaSettings;

        static void Initialize(const GFX::Context& context);
        static void UpdateContext(const GFX::Context& context);
        static void Shutdown();

        static void UpdateCommonParams(float deltaTime);
        static void SetIntensity(float intensity);
        static void SetCombo(float combo);
        static void ClearEffects();
        static void AddEffect(PostEffect* effect);
        static bool SetGlobalProfile(
            const std::string& profileId,
            const DirectX::XMFLOAT4(&values)[16]);
        static void ClearGlobalProfile();
        static void SetBloomSettings(const BloomSettings& settings);
        static void SetToneMappingSettings(const ToneMappingSettings& settings);
        static void SetFxaaSettings(const FxaaSettings& settings);
        static const FxaaSettings& GetFxaaSettings();
        static const BloomDebugStats& GetBloomDebugStats();
        static void SetTransitionState(const TransitionVisualState& state);
        static void ClearTransitionState();

        static std::string DumpFrameState();
        static void LogFrameState(const char* reason);
        static void RequestFrameDump();

        static void BeginSceneCapture();
        static bool IsSceneCaptureActive();
        static bool HasCurrentRenderTarget();
        static bool RebindCurrentRenderTarget();
        static D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetDepthSrv();
        static D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetDsv();
        static D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetReadOnlyDsv();
        static bool BeginCurrentRenderTargetDepthRead();
        static void EndCurrentRenderTargetDepthRead();
        static bool PrepareGameUiComposition(
            bool editorViewport,
            int uiLogicalWidth = 0,
            int uiLogicalHeight = 0);
        static bool FinalizeGameUiComposition(bool editorViewport);
        static const PresentationFrameResources& GetPresentationFrameResources();
        static void EndSceneCaptureAndPresent();
        static bool EndSceneCaptureToEditorViewport();

        static void SetSceneCaptureSize(
            int renderWidth,
            int renderHeight,
            int outputWidth,
            int outputHeight);
        static void GetSceneCaptureSize(int& width, int& height);
        static void GetSceneOutputSize(int& width, int& height);

        static bool IsEditorViewportReady();
        static D3D12_GPU_DESCRIPTOR_HANDLE GetEditorViewportSrv();
        static int GetEditorViewportWidth();
        static int GetEditorViewportHeight();

        static bool CaptureSceneColorSnapshot();
        static bool IsSceneColorReady();
        static D3D12_GPU_DESCRIPTOR_HANDLE GetSceneColorSrv();
        static int GetSceneColorWidth();
        static int GetSceneColorHeight();

        static void SetAmbientColor(float r, float g, float b);
        static void BeginLightCapture();
        static void EndLightCapture();

        static void BeginLayer(
            PostChain& chain,
            float r = 0,
            float g = 0,
            float b = 0,
            float a = 0);
        static void EndLayer(BlendOption blendMode = BlendOption::Alpha);

    private:
        static RenderTarget2D* EndSceneCaptureAndResolveFinal();
        static RenderTarget2D* ResolveFinalSceneToLdr(
            RenderTarget2D& source,
            DXGI_FORMAT outputFormat);

        static bool initialized_;
        static GFX::Context context_;
        static QuadDrawer quad_;
        static SceneCaptureStage captureStage_;
        static PostProcessingStage processingStage_;
        static GameUiCompositionStage gameUiCompositionStage_;
        static PostPresentationStage presentationStage_;
        static int requestedRenderWidth_;
        static int requestedRenderHeight_;
        static int requestedOutputWidth_;
        static int requestedOutputHeight_;
    };

} // namespace HIKARI::POST
