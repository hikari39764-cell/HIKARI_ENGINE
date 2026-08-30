#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"
#include "Vfx/Post/HIKARI_SceneColorSnapshot.h"

namespace HIKARI::POST {

    class SceneCaptureStage {
    public:
        void UpdateContext(const GFX::Context& context);
        void Shutdown();

        bool Begin(int width, int height);
        RenderTarget2D* End();
        bool Abort();
        bool IsActive() const;
        bool HasCurrentTarget() const;
        bool RebindCurrentTarget();
        RENDER3D::RenderTextureView GetCurrentDepthView() const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentDsv() const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentReadOnlyDsv() const;
        bool BeginCurrentDepthRead();
        void EndCurrentDepthRead();

        RENDER3D::RenderTextureView CaptureSceneColorSnapshot(
            SceneColorSnapshotKind kind);
        RENDER3D::RenderTextureView GetSceneColorSnapshotView(
            SceneColorSnapshotKind kind) const;

        void SetAmbientColor(float r, float g, float b);
        void BeginLightCapture();
        void EndLightCapture();
        bool IsLightingEnabled() const;
        RenderTarget2D* GetLightTarget();

        std::string DumpState() const;

    private:
        static constexpr size_t kSnapshotKindCount =
            static_cast<size_t>(SceneColorSnapshotKind::Count);

        bool EnsureSceneTargets(int width, int height);
        bool EnsureSceneColorSnapshot(SceneColorSnapshotKind kind);
        void RefreshSceneColorSrv(SceneColorSnapshotKind kind);

        GFX::Context context_{};
        RenderTarget2D sceneTarget_{};
        std::array<
            RenderTarget2D,
            kSnapshotKindCount>
            sceneColorSnapshots_{};
        RenderTarget2D lightTarget_{};
        RenderTarget2D* currentTarget_ = nullptr;
        std::array<
            std::array<
                D3D12_CPU_DESCRIPTOR_HANDLE,
                GFX::kFrameResourceCount>,
            kSnapshotKindCount>
            sceneColorSrvCpu_{};
        std::array<
            std::array<
                D3D12_GPU_DESCRIPTOR_HANDLE,
                GFX::kFrameResourceCount>,
            kSnapshotKindCount>
            sceneColorSrvGpu_{};
        float ambientColor_[3] = { 1.0f, 1.0f, 1.0f };
        float lightClearColor_[3] = { 1.0f, 1.0f, 1.0f };
        bool active_ = false;
        std::array<
            bool,
            kSnapshotKindCount>
            sceneColorReady_{};
        bool lightingEnabled_ = false;
    };

} // namespace HIKARI::POST
