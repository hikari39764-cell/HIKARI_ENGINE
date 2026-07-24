#pragma once

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"

namespace HIKARI::POST {

    class QuadDrawer;

    class PostPresentationStage {
    public:
        void UpdateContext(const GFX::Context& context);
        void Shutdown();
        void InvalidateEditorOutput();

        bool PresentToEditor(
            RenderTarget2D& source,
            QuadDrawer& quad,
            int expectedWidth,
            int expectedHeight);
        bool PresentToBackBuffer(RenderTarget2D& source, QuadDrawer& quad);
        void BindBackBufferFullViewport() const;

        bool IsEditorOutputReady(int expectedWidth, int expectedHeight) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetEditorOutputSrv() const;
        int GetEditorOutputWidth() const;
        int GetEditorOutputHeight() const;

    private:
        bool EnsureEditorOutput(int width, int height);
        bool DrawSource(RenderTarget2D& source, QuadDrawer& quad) const;
        void RefreshEditorOutputSrv();

        GFX::Context context_{};
        RenderTarget2D editorOutput_{};
        D3D12_CPU_DESCRIPTOR_HANDLE editorOutputSrvCpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE editorOutputSrvGpu_{};
        bool editorOutputReady_ = false;
    };

} // namespace HIKARI::POST
