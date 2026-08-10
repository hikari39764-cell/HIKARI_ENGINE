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
        bool Initialize(
            const GFX::Context& context,
            uint32_t particleCapacity,
            uint32_t emitterPageCapacity);
        void Shutdown(const GFX::Context& context) noexcept;

        bool IsReady() const noexcept;
        void Transition(
            const GFX::Context& context,
            D3D12_RESOURCE_STATES nextState);
        void InsertUavBarrier(const GFX::Context& context) const;

        bool WriteEmitterParameters(
            ID3D12Device* device,
            uint32_t frameSlot,
            GpuParticleEmitterUploadAddress address,
            const GpuParticleEmitterParameters& parameters);
        D3D12_GPU_VIRTUAL_ADDRESS GetEmitterUploadGpuAddress(
            uint32_t frameSlot,
            uint32_t pageIndex) const noexcept;
        D3D12_GPU_DESCRIPTOR_HANDLE GetParticleSrvGpuHandle()
            const noexcept;
        D3D12_GPU_DESCRIPTOR_HANDLE GetParticleUavGpuHandle()
            const noexcept;

    private:
        bool EnsureEmitterUploadPage(
            ID3D12Device* device,
            uint32_t frameSlot,
            uint32_t pageIndex);

        Microsoft::WRL::ComPtr<ID3D12Resource> particles_{};
        RenderResourceView particleSrv_{};
        RenderResourceView particleUav_{};
        std::array<
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>,
            GFX::kFrameResourceCount> emitterUploadPages_{};
        std::array<
            std::vector<GpuParticleEmitterParameters*>,
            GFX::kFrameResourceCount> mappedEmitterPages_{};
        uint32_t emitterPageCapacity_ = 0u;
        D3D12_RESOURCE_STATES particleState_ =
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    };

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
