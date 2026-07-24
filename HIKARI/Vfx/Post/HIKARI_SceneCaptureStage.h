#pragma once

#include <stack>
#include <string>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::POST {

    class PostChain;
    struct CommonParams;

    class SceneCaptureStage {
    public:
        void UpdateContext(const GFX::Context& context);
        void Shutdown();

        bool Begin(int width, int height);
        RenderTarget2D* End();
        bool IsActive() const;
        bool HasCurrentTarget() const;
        bool RebindCurrentTarget();
        D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentDepthSrv() const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentDsv() const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentReadOnlyDsv() const;
        bool BeginCurrentDepthRead();
        void EndCurrentDepthRead();

        bool CaptureSceneColorSnapshot();
        bool IsSceneColorReady() const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetSceneColorSrv() const;
        int GetSceneColorWidth() const;
        int GetSceneColorHeight() const;

        void SetAmbientColor(float r, float g, float b);
        void BeginLightCapture();
        void EndLightCapture();
        bool IsLightingEnabled() const;
        RenderTarget2D* GetLightTarget();

        void BeginLayer(
            PostChain& chain,
            float r,
            float g,
            float b,
            float a);
        void EndLayer(
            BlendOption blendMode,
            QuadDrawer& quad,
            const CommonParams& commonParams);

        std::string DumpState() const;

    private:
        struct LayerInfo {
            RenderTarget2D* target = nullptr;
            PostChain* chain = nullptr;
        };

        bool EnsureSceneTargets(int width, int height);
        bool EnsureSceneColorSnapshot();
        void RefreshSceneColorSrv();

        GFX::Context context_{};
        RenderTarget2D sceneTarget_{};
        RenderTarget2D sceneColorSnapshot_{};
        RenderTarget2D lightTarget_{};
        std::stack<LayerInfo> layers_{};
        D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSrvCpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvGpu_{};
        float ambientColor_[3] = { 1.0f, 1.0f, 1.0f };
        float lightClearColor_[3] = { 1.0f, 1.0f, 1.0f };
        bool active_ = false;
        bool sceneColorReady_ = false;
        bool lightingEnabled_ = false;
    };

} // namespace HIKARI::POST
