#pragma once

#include <array>
#include <d3d12.h>
#include <wrl/client.h>
#include "Gfx/HIKARI_GfxContext.h"
#include "RenderCore/Resources/HIKARI_RenderResourcePool.h"
#include "RenderCore/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI::RENDER3D::SURFACE {

// Forward が生成する捕捉出力（反射分量と次表面散乱の拡散光）の保管だけを所有する。追跡・合成・履歴は所有しない。
// 出力の並びは PSO の RT 1..3 と同じ：fallback 鏡面・鏡面応答・次表面散乱（任意）。
class SceneReflectionBuffer final {
public:
    SceneReflectionBuffer() = default;
    SceneReflectionBuffer(const SceneReflectionBuffer&) = delete;
    SceneReflectionBuffer& operator=(const SceneReflectionBuffer&) = delete;
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    bool Ensure(const GFX::Context& graphics, uint32_t width, uint32_t height, bool enabled,
                bool captureSubsurface = false);
    bool BeginWrite(ID3D12GraphicsCommandList* commands);
    void EndWrite(ID3D12GraphicsCommandList* commands, bool succeeded);
    void Release();
    static constexpr size_t kTargetCount = 3u;
    const std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kTargetCount>& GetRtvs() const { return rtvs_; }
    RenderTextureView GetFallbackSpecularView() const { return GetView(0); }
    RenderTextureView GetSpecularResponseView() const { return GetView(1); }
    RenderTextureView GetSubsurfaceView() const { return GetView(2); } // RGB=拡散光 × 強さ, A=散乱半径
    uint64_t GetWorkingSetBytes() const { return workingSetBytes_; }

private:
    bool PublishDescriptors(const GFX::Context& graphics);
    void Discard(bool releaseDescriptors, bool completed = false);
    void Transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES nextState);
    RenderTextureView GetView(size_t index) const;

    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kTargetCount> textures_{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kTargetCount> rtvs_{};
    std::array<std::array<RenderResourceView, kTargetCount>, GFX::kFrameResourceCount> srvs_{};
    std::array<uint64_t, GFX::kFrameResourceCount> descriptorGenerations_{};
    GFX::Context context_{};
    uint64_t generation_ = 0;
    uint64_t lastRetireFence_ = 0, workingSetBytes_ = 0;
    uint32_t width_ = 0, height_ = 0, frameSlot_ = 0;
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    bool ready_ = false, written_ = false;
};

} // namespace HIKARI::RENDER3D::SURFACE
