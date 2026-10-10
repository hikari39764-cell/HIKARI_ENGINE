#pragma once

#include <array>
#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"

namespace HIKARI::POST {

class QuadDrawer;

class PostPresentationStage {
public:
    void UpdateContext(const GFX::Context& context);
    void Shutdown();

    bool PresentToEditor(RenderTarget2D& source, QuadDrawer& quad, int expectedWidth, int expectedHeight);
    bool PresentToBackBuffer(RenderTarget2D& source, QuadDrawer& quad);
    void BindBackBufferFullViewport() const;

    bool IsEditorOutputReady(int expectedWidth, int expectedHeight) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetEditorOutputSrv() const;

    // 布景の A/B 分屏：次に編集器へ出す画を A として複製して残す（編集の補助線は含まない）。
    void RequestEditorSnapshot() noexcept { editorSnapshotRequested_ = true; }
    void ClearEditorSnapshot();
    // 残した A の画（無ければ 0）。
    D3D12_GPU_DESCRIPTOR_HANDLE GetEditorSnapshotSrv() const noexcept { return editorSnapshotSrv_; }

private:
    bool CaptureEditorSnapshot(RenderTarget2D& source, QuadDrawer& quad);
    bool HasCompatibleEditorOutput(int width, int height) const;
    bool DrawSource(RenderTarget2D& source, QuadDrawer& quad) const;
    bool RefreshEditorOutputSrv();
    void ResetEditorOutputSrvTracking() noexcept;
    void PruneObsoleteEditorOutputSrvTracking(ID3D12Resource* currentResource) noexcept;

    GFX::Context context_{};
    RenderTarget2D editorOutput_{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> editorOutputSrvCpu_{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> editorOutputSrvGpu_{};
    std::array<ID3D12Resource*, GFX::kFrameResourceCount> editorOutputSrvResources_{};
    std::array<uint64_t, GFX::kFrameResourceCount> editorOutputSrvGenerations_{};
    std::array<bool, GFX::kFrameResourceCount> editorOutputSrvReady_{};
    uint32_t lastPublishedFrameSlot_ = GFX::kFrameResourceCount;
    RenderTarget2D editorSnapshot_{};
    D3D12_GPU_DESCRIPTOR_HANDLE editorSnapshotSrv_{};
    bool editorSnapshotRequested_ = false;
};

} // namespace HIKARI::POST
