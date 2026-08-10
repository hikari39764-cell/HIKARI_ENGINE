#include "Render3D/Vfx/Internal/HIKARI_GpuParticleStorage.h"

#include <utility>

#include <d3dx12.h>

#include "Gfx/HIKARI_ResourceStateTracker.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleShaderInterop.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleUploadBuffer.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {
    namespace {
        using Microsoft::WRL::ComPtr;

    }

    bool GpuParticleStorage::Initialize(
        const GFX::Context& context,
        uint32_t particleCapacity,
        uint32_t emitterPageCapacity) {

        Shutdown(context);
        if (context.device == nullptr || particleCapacity == 0u ||
            emitterPageCapacity == 0u) {
            return false;
        }
        emitterPageCapacity_ = emitterPageCapacity;
        const auto defaultHeap =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto particleDescription = CD3DX12_RESOURCE_DESC::Buffer(
            static_cast<UINT64>(particleCapacity) * sizeof(GpuParticle),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (!HIKARI_DX_CHECK(
                context.device->CreateCommittedResource(
                    &defaultHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &particleDescription,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    nullptr,
                    IID_PPV_ARGS(particles_.GetAddressOf())),
                "GpuParticleRenderer::CreateParticleBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(particles_.Get(), L"HIKARI GPU Particle Pool");
        particleSrv_ = AllocateBufferSrvDescriptor(
            particles_.Get(),
            DXGI_FORMAT_UNKNOWN,
            particleCapacity,
            sizeof(GpuParticle));
        particleUav_ = AllocateBufferUavDescriptor(
            particles_.Get(),
            DXGI_FORMAT_UNKNOWN,
            particleCapacity,
            sizeof(GpuParticle));
        if (!particleSrv_.IsValid() || !particleUav_.IsValid()) {
            return false;
        }
        if (context.resourceStates != nullptr) {
            context.resourceStates->Track(
                particles_.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        particleState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        for (uint32_t index = 0u;
             index < GFX::kFrameResourceCount;
             ++index) {
            if (!EnsureEmitterUploadPage(context.device, index, 0u)) {
                return false;
            }
        }
        return true;
    }

    void GpuParticleStorage::Shutdown(
        const GFX::Context& context) noexcept {
        if (context.resourceStates != nullptr && particles_ != nullptr) {
            context.resourceStates->Forget(particles_.Get());
        }
        if (particleSrv_.IsValid()) {
            (void)ReleaseRenderResourceDescriptor(particleSrv_);
        }
        if (particleUav_.IsValid()) {
            (void)ReleaseRenderResourceDescriptor(particleUav_);
        }
        particleSrv_ = {};
        particleUav_ = {};
        for (uint32_t frameSlot = 0u;
             frameSlot < GFX::kFrameResourceCount;
             ++frameSlot) {
            for (size_t pageIndex = 0u;
                 pageIndex < emitterUploadPages_[frameSlot].size();
                 ++pageIndex) {
                if (emitterUploadPages_[frameSlot][pageIndex] != nullptr &&
                    pageIndex < mappedEmitterPages_[frameSlot].size() &&
                    mappedEmitterPages_[frameSlot][pageIndex] != nullptr) {
                    emitterUploadPages_[frameSlot][pageIndex]->Unmap(
                        0u,
                        nullptr);
                }
            }
            mappedEmitterPages_[frameSlot].clear();
            emitterUploadPages_[frameSlot].clear();
        }
        particles_.Reset();
        emitterPageCapacity_ = 0u;
        particleState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    bool GpuParticleStorage::IsReady() const noexcept {
        return particles_ != nullptr;
    }

    void GpuParticleStorage::Transition(
        const GFX::Context& context,
        D3D12_RESOURCE_STATES nextState) {
        if (particles_ == nullptr || context.cmdList == nullptr ||
            particleState_ == nextState) {
            return;
        }
        if (context.resourceStates != nullptr) {
            context.resourceStates->Transition(
                context.cmdList,
                particles_.Get(),
                particleState_,
                nextState);
        } else {
            const D3D12_RESOURCE_BARRIER barrier =
                CD3DX12_RESOURCE_BARRIER::Transition(
                    particles_.Get(),
                    particleState_,
                    nextState);
            context.cmdList->ResourceBarrier(1u, &barrier);
        }
        particleState_ = nextState;
    }

    void GpuParticleStorage::InsertUavBarrier(
        const GFX::Context& context) const {
        if (context.cmdList == nullptr || particles_ == nullptr) {
            return;
        }
        const D3D12_RESOURCE_BARRIER barrier =
            CD3DX12_RESOURCE_BARRIER::UAV(particles_.Get());
        context.cmdList->ResourceBarrier(1u, &barrier);
    }

    bool GpuParticleStorage::WriteEmitterParameters(
        ID3D12Device* device,
        uint32_t frameSlot,
        GpuParticleEmitterUploadAddress address,
        const GpuParticleEmitterParameters& parameters) {
        if (!address.valid ||
            !EnsureEmitterUploadPage(
                device,
                frameSlot,
                address.pageIndex)) {
            return false;
        }
        mappedEmitterPages_[frameSlot]
            [address.pageIndex][address.elementIndex] = parameters;
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS
        GpuParticleStorage::GetEmitterUploadGpuAddress(
            uint32_t frameSlot,
            uint32_t pageIndex) const noexcept {
        if (frameSlot >= GFX::kFrameResourceCount ||
            pageIndex >= emitterUploadPages_[frameSlot].size() ||
            emitterUploadPages_[frameSlot][pageIndex] == nullptr) {
            return 0u;
        }
        return emitterUploadPages_[frameSlot][pageIndex]
            ->GetGPUVirtualAddress();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE
        GpuParticleStorage::GetParticleSrvGpuHandle() const noexcept {
        return particleSrv_.gpu;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE
        GpuParticleStorage::GetParticleUavGpuHandle() const noexcept {
        return particleUav_.gpu;
    }

    bool GpuParticleStorage::EnsureEmitterUploadPage(
        ID3D12Device* device,
        uint32_t frameSlot,
        uint32_t pageIndex) {
        if (frameSlot >= GFX::kFrameResourceCount ||
            pageIndex >= kGpuParticleEmitterUploadPageCount) {
            return false;
        }
        auto& resources = emitterUploadPages_[frameSlot];
        auto& mappings = mappedEmitterPages_[frameSlot];
        while (resources.size() <= pageIndex) {
            ComPtr<ID3D12Resource> resource{};
            GpuParticleEmitterParameters* mapped = nullptr;
            if (!CreateGpuParticleMappedUploadBuffer(
                    device,
                    emitterPageCapacity_,
                    "GpuParticleRenderer::CreateEmitterUploadPage",
                    resource,
                    mapped)) {
                return false;
            }
            resources.push_back(std::move(resource));
            mappings.push_back(mapped);
        }
        return true;
    }

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
