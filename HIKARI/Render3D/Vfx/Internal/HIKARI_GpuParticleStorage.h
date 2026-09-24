#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"
#include "Render3D/Vfx/HIKARI_GpuParticleParameterPacking.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {

class GpuParticleStorage final {
public:
    bool Initialize(const GFX::Context& context, uint32_t particleCapacity, uint32_t emitterPageCapacity,
                    uint32_t drawCapacity = kMaximumParticleEmitterSubmissionsPerFrame);
    void Shutdown(const GFX::Context& context) noexcept;

    bool IsReady() const noexcept;
    void Transition(const GFX::Context& context, D3D12_RESOURCE_STATES nextState);
    void InsertUavBarrier(const GFX::Context& context) const;
    void InsertDrawResetUavBarrier(const GFX::Context& context) const;
    void InsertDrawCounterUavBarrier(const GFX::Context& context) const;
    void TransitionDrawCounters(const GFX::Context& context, D3D12_RESOURCE_STATES nextState);
    void TransitionDrawArguments(const GFX::Context& context, D3D12_RESOURCE_STATES nextState);
    void TransitionAliveIndices(const GFX::Context& context, D3D12_RESOURCE_STATES nextState);

    bool WriteEmitterParameters(ID3D12Device* device, uint32_t frameSlot, GpuParticleEmitterUploadAddress address,
                                const GpuParticleEmitterParameters& parameters);
    D3D12_GPU_VIRTUAL_ADDRESS GetEmitterUploadGpuAddress(uint32_t frameSlot, uint32_t pageIndex) const noexcept;
    D3D12_GPU_DESCRIPTOR_HANDLE GetParticleSrvGpuHandle() const noexcept;
    D3D12_GPU_DESCRIPTOR_HANDLE GetParticleUavGpuHandle() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS GetParticleGpuAddress() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS GetAliveIndexGpuAddress() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS GetDrawCounterGpuAddress() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS GetDrawArgumentGpuAddress() const noexcept;
    ID3D12Resource* GetDrawArgumentResource() const noexcept;
    // 形状パラメータは per-draw バッファで渡す。drawIndex で引く。
    D3D12_GPU_VIRTUAL_ADDRESS GetShapeStateGpuAddress(uint32_t frameSlot) const noexcept;
    GpuParticleShapeState* MappedShapeStates(uint32_t frameSlot) noexcept;

private:
    bool EnsureEmitterUploadPage(ID3D12Device* device, uint32_t frameSlot, uint32_t pageIndex);

    Microsoft::WRL::ComPtr<ID3D12Resource> particles_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> aliveIndices_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> drawCounters_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> drawArguments_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, GFX::kFrameResourceCount> shapeStates_{};
    std::array<GpuParticleShapeState*, GFX::kFrameResourceCount> mappedShapeStates_{};
    RenderResourceView particleSrv_{};
    RenderResourceView particleUav_{};
    std::array<std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>, GFX::kFrameResourceCount> emitterUploadPages_{};
    std::array<std::vector<GpuParticleEmitterParameters*>, GFX::kFrameResourceCount> mappedEmitterPages_{};
    uint32_t emitterPageCapacity_ = 0u;
    D3D12_RESOURCE_STATES particleState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES drawCounterState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES drawArgumentState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES aliveIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
};

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
