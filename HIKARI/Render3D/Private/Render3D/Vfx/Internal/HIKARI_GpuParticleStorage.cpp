#include "Render3D/Vfx/Internal/HIKARI_GpuParticleStorage.h"
#include "Gfx/HIKARI_UploadTraffic.h"

#include <array>
#include <utility>

#include <d3dx12.h>

#include "Gfx/HIKARI_ResourceStateTracker.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleShaderInterop.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleUploadBuffer.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {
namespace {
using Microsoft::WRL::ComPtr;

void TransitionBuffer(const GFX::Context& context, ID3D12Resource* resource, D3D12_RESOURCE_STATES& state,
                      D3D12_RESOURCE_STATES nextState) {
    if (context.cmdList == nullptr || resource == nullptr || state == nextState)
        return;
    if (context.resourceStates != nullptr)
        context.resourceStates->Transition(context.cmdList, resource, state, nextState);
    else {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, state, nextState);
        context.cmdList->ResourceBarrier(1u, &barrier);
    }
    state = nextState;
}

} // namespace

bool GpuParticleStorage::Initialize(const GFX::Context& context, uint32_t particleCapacity,
                                    uint32_t emitterPageCapacity, uint32_t drawCapacity) {
    Shutdown(context);
    if (context.device == nullptr || particleCapacity == 0u || emitterPageCapacity == 0u || drawCapacity == 0u) {
        return false;
    }
    emitterPageCapacity_ = emitterPageCapacity;
    const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto particleDescription = CD3DX12_RESOURCE_DESC::Buffer(
        static_cast<UINT64>(particleCapacity) * sizeof(GpuParticle), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                             context.device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDescription,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, GFX::VIDEO_MEMORY::Owner::Vfx,
                             IID_PPV_ARGS(particles_.GetAddressOf())),
                         "ParticleGpuDrivenPass::CreateParticleBuffer")) {
        return false;
    }
    GFX::SetD3D12Name(particles_.Get(), L"HIKARI GPU Particle Pool");
    particleSrv_ =
        AllocateBufferSrvDescriptor(particles_.Get(), DXGI_FORMAT_UNKNOWN, particleCapacity, sizeof(GpuParticle));
    particleUav_ =
        AllocateBufferUavDescriptor(particles_.Get(), DXGI_FORMAT_UNKNOWN, particleCapacity, sizeof(GpuParticle));
    if (!particleSrv_.IsValid() || !particleUav_.IsValid()) {
        return false;
    }
    // 形状パラメータは per-draw バッファで渡す。ルート定数のコスト上限が
    // 厳しく、形状側のパラメータは今後も増えるため。
    for (uint32_t frameSlot = 0u; frameSlot < GFX::kFrameResourceCount; ++frameSlot) {
        if (!CreateGpuParticleMappedUploadBuffer(context.device, drawCapacity,
                                                 "ParticleGpuDrivenPass::CreateShapeStateBuffer",
                                                 shapeStates_[frameSlot], mappedShapeStates_[frameSlot])) {
            return false;
        }
    }
    if (context.resourceStates != nullptr) {
        context.resourceStates->Track(particles_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    particleState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    const auto createGpuBuffer = [&](UINT64 byteSize, const wchar_t* name, ComPtr<ID3D12Resource>& resource) {
        const auto description = CD3DX12_RESOURCE_DESC::Buffer(byteSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                                 context.device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &description,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, GFX::VIDEO_MEMORY::Owner::Vfx,
                                 IID_PPV_ARGS(resource.GetAddressOf())),
                             "ParticleGpuDrivenPass::CreateDrawBuffer")) {
            return false;
        }
        GFX::SetD3D12Name(resource.Get(), name);
        if (context.resourceStates != nullptr) {
            context.resourceStates->Track(resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        return true;
    };
    if (!createGpuBuffer(static_cast<UINT64>(particleCapacity) * sizeof(uint32_t),
                         L"HIKARI Particle Alive Index Buffer", aliveIndices_) ||
        !createGpuBuffer(static_cast<UINT64>(drawCapacity) * sizeof(uint32_t), L"HIKARI Particle Draw Counter Buffer",
                         drawCounters_) ||
        !createGpuBuffer(static_cast<UINT64>(drawCapacity) * kGpuParticleDrawArgumentStride,
                         L"HIKARI Particle Draw Argument Buffer", drawArguments_)) {
        return false;
    }
    drawArgumentState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    drawCounterState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    aliveIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    for (uint32_t index = 0u; index < GFX::kFrameResourceCount; ++index) {
        if (!EnsureEmitterUploadPage(context.device, index, 0u) ||
            !CreateGpuParticleMappedUploadBuffer(context.device, 1u, "ParticleGpuDrivenPass::CreateCollisionView",
                                                 collisionViews_[index], mappedCollisionViews_[index])) {
            return false;
        }
        *mappedCollisionViews_[index] = {};
    }
    nullCollisionDepthSrv_ = AllocateRenderResourceDescriptor();
    nullCollisionDepthUav_ = AllocateRenderResourceDescriptor();
    if (!nullCollisionDepthSrv_.IsValid() || !nullCollisionDepthUav_.IsValid()) {
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
    nullSrv.Format = DXGI_FORMAT_R32_FLOAT;
    nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrv.Texture2D.MipLevels = 1u;
    context.device->CreateShaderResourceView(nullptr, &nullSrv, nullCollisionDepthSrv_.cpu);
    D3D12_UNORDERED_ACCESS_VIEW_DESC nullUav{};
    nullUav.Format = DXGI_FORMAT_R32_FLOAT;
    nullUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    context.device->CreateUnorderedAccessView(nullptr, nullptr, &nullUav, nullCollisionDepthUav_.cpu);
    return true;
}

bool GpuParticleStorage::EnsureCollisionDepth(const GFX::Context& context, uint32_t width, uint32_t height) {
    if (context.device == nullptr || width == 0u || height == 0u)
        return false;
    if (collisionDepth_ != nullptr && collisionDepthWidth_ == width && collisionDepthHeight_ == height)
        return true;
    if (context.resourceStates != nullptr && collisionDepth_ != nullptr)
        context.resourceStates->Forget(collisionDepth_.Get());
    for (RenderResourceView* view : {&collisionDepthSrv_, &collisionDepthUav_}) {
        if (view->IsValid())
            (void)ReleaseRenderResourceDescriptor(*view);
        *view = {};
    }
    collisionDepth_.Reset();
    collisionDepthWidth_ = collisionDepthHeight_ = 0u;
    const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto description = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, width, height, 1u, 1u, 1u, 0u,
                                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(context.device, &defaultHeap, D3D12_HEAP_FLAG_NONE,
                                                                    &description, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                    nullptr, GFX::VIDEO_MEMORY::Owner::Vfx,
                                                                    IID_PPV_ARGS(collisionDepth_.GetAddressOf())),
                         "ParticleGpuDrivenPass::CreateCollisionDepth")) {
        return false;
    }
    GFX::SetD3D12Name(collisionDepth_.Get(), L"HIKARI Particle Collision Depth");
    collisionDepthSrv_ = AllocateTexture2DSrvDescriptor(collisionDepth_.Get(), DXGI_FORMAT_R32_FLOAT);
    collisionDepthUav_ = AllocateTexture2DUavDescriptor(collisionDepth_.Get(), DXGI_FORMAT_R32_FLOAT);
    if (!collisionDepthSrv_.IsValid() || !collisionDepthUav_.IsValid()) {
        collisionDepth_.Reset();
        return false;
    }
    if (context.resourceStates != nullptr)
        context.resourceStates->Track(collisionDepth_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    collisionDepthState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    collisionDepthWidth_ = width;
    collisionDepthHeight_ = height;
    return true;
}

void GpuParticleStorage::TransitionCollisionDepth(const GFX::Context& context, D3D12_RESOURCE_STATES nextState) {
    TransitionBuffer(context, collisionDepth_.Get(), collisionDepthState_, nextState);
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuParticleStorage::GetCollisionDepthSrv() const noexcept {
    return collisionDepthSrv_.IsValid() ? collisionDepthSrv_.gpu : nullCollisionDepthSrv_.gpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuParticleStorage::GetCollisionDepthUav() const noexcept {
    return collisionDepthUav_.IsValid() ? collisionDepthUav_.gpu : nullCollisionDepthUav_.gpu;
}

void GpuParticleStorage::WriteCollisionView(uint32_t frameSlot, const GpuParticleCollisionView& view) {
    if (frameSlot >= GFX::kFrameResourceCount || mappedCollisionViews_[frameSlot] == nullptr)
        return;
    *mappedCollisionViews_[frameSlot] = view;
    GFX::UPLOAD_TRAFFIC::Record(GFX::UPLOAD_TRAFFIC::Owner::Particles, sizeof(view));
}

D3D12_GPU_VIRTUAL_ADDRESS GpuParticleStorage::GetCollisionViewGpuAddress(uint32_t frameSlot) const noexcept {
    return frameSlot < GFX::kFrameResourceCount && collisionViews_[frameSlot] != nullptr
               ? collisionViews_[frameSlot]->GetGPUVirtualAddress()
               : 0u;
}

void GpuParticleStorage::Shutdown(const GFX::Context& context) noexcept {
    if (context.resourceStates != nullptr) {
        if (particles_ != nullptr) {
            context.resourceStates->Forget(particles_.Get());
        }
        if (aliveIndices_ != nullptr) {
            context.resourceStates->Forget(aliveIndices_.Get());
        }
        if (drawCounters_ != nullptr) {
            context.resourceStates->Forget(drawCounters_.Get());
        }
        if (drawArguments_ != nullptr) {
            context.resourceStates->Forget(drawArguments_.Get());
        }
    }
    if (context.resourceStates != nullptr && collisionDepth_ != nullptr)
        context.resourceStates->Forget(collisionDepth_.Get());
    for (RenderResourceView* view :
         {&collisionDepthSrv_, &collisionDepthUav_, &nullCollisionDepthSrv_, &nullCollisionDepthUav_}) {
        if (view->IsValid())
            (void)ReleaseRenderResourceDescriptor(*view);
        *view = {};
    }
    collisionDepth_.Reset();
    collisionDepthWidth_ = collisionDepthHeight_ = 0u;
    for (uint32_t frameSlot = 0u; frameSlot < GFX::kFrameResourceCount; ++frameSlot) {
        if (collisionViews_[frameSlot] != nullptr && mappedCollisionViews_[frameSlot] != nullptr)
            collisionViews_[frameSlot]->Unmap(0u, nullptr);
        mappedCollisionViews_[frameSlot] = nullptr;
        collisionViews_[frameSlot].Reset();
    }
    if (particleSrv_.IsValid()) {
        (void)ReleaseRenderResourceDescriptor(particleSrv_);
    }
    if (particleUav_.IsValid()) {
        (void)ReleaseRenderResourceDescriptor(particleUav_);
    }
    particleSrv_ = {};
    particleUav_ = {};
    for (uint32_t frameSlot = 0u; frameSlot < GFX::kFrameResourceCount; ++frameSlot) {
        if (shapeStates_[frameSlot] != nullptr && mappedShapeStates_[frameSlot] != nullptr) {
            shapeStates_[frameSlot]->Unmap(0u, nullptr);
        }
        mappedShapeStates_[frameSlot] = nullptr;
        shapeStates_[frameSlot].Reset();
    }
    for (uint32_t frameSlot = 0u; frameSlot < GFX::kFrameResourceCount; ++frameSlot) {
        for (size_t pageIndex = 0u; pageIndex < emitterUploadPages_[frameSlot].size(); ++pageIndex) {
            if (emitterUploadPages_[frameSlot][pageIndex] != nullptr &&
                pageIndex < mappedEmitterPages_[frameSlot].size() &&
                mappedEmitterPages_[frameSlot][pageIndex] != nullptr) {
                emitterUploadPages_[frameSlot][pageIndex]->Unmap(0u, nullptr);
            }
        }
        mappedEmitterPages_[frameSlot].clear();
        emitterUploadPages_[frameSlot].clear();
    }
    particles_.Reset();
    aliveIndices_.Reset();
    drawCounters_.Reset();
    drawArguments_.Reset();
    emitterPageCapacity_ = 0u;
    particleState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    drawArgumentState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    drawCounterState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    aliveIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

bool GpuParticleStorage::IsReady() const noexcept {
    return particles_ != nullptr && aliveIndices_ != nullptr && drawCounters_ != nullptr && drawArguments_ != nullptr;
}

void GpuParticleStorage::Transition(const GFX::Context& context, D3D12_RESOURCE_STATES nextState) {
    TransitionBuffer(context, particles_.Get(), particleState_, nextState);
}

void GpuParticleStorage::InsertUavBarrier(const GFX::Context& context) const {
    if (context.cmdList == nullptr || particles_ == nullptr) {
        return;
    }
    const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(particles_.Get());
    context.cmdList->ResourceBarrier(1u, &barrier);
}

void GpuParticleStorage::InsertDrawResetUavBarrier(const GFX::Context& context) const {
    if (context.cmdList == nullptr || drawCounters_ == nullptr || drawArguments_ == nullptr) {
        return;
    }
    // ResetDrawCS writes only the counter and indirect argument. Order
    // those writes before CompactDrawCS without stalling the independent
    // alive-index buffer.
    const std::array barriers{
        CD3DX12_RESOURCE_BARRIER::UAV(drawCounters_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(drawArguments_.Get()),
    };
    context.cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void GpuParticleStorage::InsertDrawCounterUavBarrier(const GFX::Context& context) const {
    if (context.cmdList == nullptr || drawCounters_ == nullptr) {
        return;
    }
    // BuildDrawArgsCS consumes only the compacted counter. Alive indices
    // and arguments are ordered by their final UAV state transitions.
    const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(drawCounters_.Get());
    context.cmdList->ResourceBarrier(1u, &barrier);
}

void GpuParticleStorage::TransitionDrawCounters(const GFX::Context& context, D3D12_RESOURCE_STATES nextState) {
    TransitionBuffer(context, drawCounters_.Get(), drawCounterState_, nextState);
}

void GpuParticleStorage::TransitionDrawArguments(const GFX::Context& context, D3D12_RESOURCE_STATES nextState) {
    TransitionBuffer(context, drawArguments_.Get(), drawArgumentState_, nextState);
}

void GpuParticleStorage::TransitionAliveIndices(const GFX::Context& context, D3D12_RESOURCE_STATES nextState) {
    TransitionBuffer(context, aliveIndices_.Get(), aliveIndexState_, nextState);
}

bool GpuParticleStorage::WriteEmitterParameters(ID3D12Device* device, uint32_t frameSlot,
                                                GpuParticleEmitterUploadAddress address,
                                                const GpuParticleEmitterParameters& parameters) {
    if (!address.valid || !EnsureEmitterUploadPage(device, frameSlot, address.pageIndex)) {
        return false;
    }
    mappedEmitterPages_[frameSlot][address.pageIndex][address.elementIndex] = parameters;
    GFX::UPLOAD_TRAFFIC::Record(GFX::UPLOAD_TRAFFIC::Owner::Particles, sizeof(parameters));
    return true;
}

D3D12_GPU_VIRTUAL_ADDRESS
GpuParticleStorage::GetEmitterUploadGpuAddress(uint32_t frameSlot, uint32_t pageIndex) const noexcept {
    if (frameSlot >= GFX::kFrameResourceCount || pageIndex >= emitterUploadPages_[frameSlot].size() ||
        emitterUploadPages_[frameSlot][pageIndex] == nullptr) {
        return 0u;
    }
    return emitterUploadPages_[frameSlot][pageIndex]->GetGPUVirtualAddress();
}

D3D12_GPU_DESCRIPTOR_HANDLE
GpuParticleStorage::GetParticleUavGpuHandle() const noexcept { return particleUav_.gpu; }

D3D12_GPU_VIRTUAL_ADDRESS
GpuParticleStorage::GetShapeStateGpuAddress(uint32_t frameSlot) const noexcept {
    return frameSlot < GFX::kFrameResourceCount && shapeStates_[frameSlot] != nullptr
               ? shapeStates_[frameSlot]->GetGPUVirtualAddress()
               : 0u;
}

GpuParticleShapeState* GpuParticleStorage::MappedShapeStates(uint32_t frameSlot) noexcept {
    return frameSlot < GFX::kFrameResourceCount ? mappedShapeStates_[frameSlot] : nullptr;
}

D3D12_GPU_VIRTUAL_ADDRESS
GpuParticleStorage::GetParticleGpuAddress() const noexcept {
    return particles_ != nullptr ? particles_->GetGPUVirtualAddress() : 0u;
}

D3D12_GPU_VIRTUAL_ADDRESS
GpuParticleStorage::GetAliveIndexGpuAddress() const noexcept {
    return aliveIndices_ != nullptr ? aliveIndices_->GetGPUVirtualAddress() : 0u;
}

D3D12_GPU_VIRTUAL_ADDRESS
GpuParticleStorage::GetDrawCounterGpuAddress() const noexcept {
    return drawCounters_ != nullptr ? drawCounters_->GetGPUVirtualAddress() : 0u;
}

D3D12_GPU_VIRTUAL_ADDRESS
GpuParticleStorage::GetDrawArgumentGpuAddress() const noexcept {
    return drawArguments_ != nullptr ? drawArguments_->GetGPUVirtualAddress() : 0u;
}

ID3D12Resource* GpuParticleStorage::GetDrawArgumentResource() const noexcept { return drawArguments_.Get(); }

bool GpuParticleStorage::EnsureEmitterUploadPage(ID3D12Device* device, uint32_t frameSlot, uint32_t pageIndex) {
    if (frameSlot >= GFX::kFrameResourceCount || pageIndex >= kGpuParticleEmitterUploadPageCount) {
        return false;
    }
    auto& resources = emitterUploadPages_[frameSlot];
    auto& mappings = mappedEmitterPages_[frameSlot];
    while (resources.size() <= pageIndex) {
        ComPtr<ID3D12Resource> resource{};
        GpuParticleEmitterParameters* mapped = nullptr;
        if (!CreateGpuParticleMappedUploadBuffer(device, emitterPageCapacity_,
                                                 "ParticleGpuDrivenPass::CreateEmitterUploadPage", resource, mapped)) {
            return false;
        }
        resources.push_back(std::move(resource));
        mappings.push_back(mapped);
    }
    return true;
}

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
