#include "Render3D/Surface/HIKARI_SceneReflectionBuffer.h"
#include <d3dx12.h>
#include "Core/Numeric/HIKARI_NonZero.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::SURFACE {

bool SceneReflectionBuffer::Ensure(const GFX::Context& graphics, uint32_t width, uint32_t height, bool enabled,
                                   bool captureMaterial, bool captureSubsurface) {
    ready_ = written_ = false;
    if (!enabled) {
        if (!context_.frameFence || context_.frameFence->GetCompletedValue() >= lastRetireFence_)
            Discard(false, true);
        return false;
    }
    if (!graphics.device || !graphics.srvHeap || !graphics.deviceGeneration || !graphics.frameFence ||
        !graphics.currentFrameRetireFenceValue || graphics.frameIndex >= GFX::kFrameResourceCount || !width ||
        !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        return false;
    if (context_.device &&
        (context_.device != graphics.device || context_.deviceGeneration != graphics.deviceGeneration ||
         context_.srvHeap != graphics.srvHeap || context_.frameFence != graphics.frameFence)) {
        Discard(true);
    }
    UpdateRenderResourceDescriptorPoolContext(graphics);
    const auto pool = GetRenderResourceDescriptorPoolStats();
    if (!pool.contextAvailable || !pool.contextCompatible)
        return false;
    context_ = graphics;
    frameSlot_ = graphics.frameIndex;
    if (!textures_[0] || !textures_[1] || width_ != width || height_ != height ||
        bool(textures_[2]) != captureMaterial || bool(textures_[3]) != captureSubsurface) {
        const auto completed = graphics.frameFence->GetCompletedValue();
        if (completed == UINT64_MAX || completed < lastRetireFence_)
            return false;
        Discard(false, true);
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto desc =
            CD3DX12_RESOURCE_DESC::Tex2D(kFormat, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        const D3D12_CLEAR_VALUE clear{kFormat, {0, 0, 0, 0}};
        const std::array<bool, kTargetCount> wanted{true, true, captureMaterial, captureSubsurface};
        const uint32_t count = 2u + (captureMaterial ? 1u : 0u) + (captureSubsurface ? 1u : 0u);
        const auto bytes = graphics.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
        // MRT は scene と同寸が必要。予算超過は低解像度の誤った capture でなく provider を辞退する。
        constexpr uint64_t budget = 192ull * 1024 * 1024;
        if (!bytes || bytes > budget / count)
            return false;
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kTargetCount> replacements;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replacementHeap;
        for (size_t i = 0; i < kTargetCount; ++i) {
            if (wanted[i] && FAILED(graphics.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state_,
                                                                             &clear, IID_PPV_ARGS(&replacements[i]))))
                return false;
        }
        const D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kTargetCount};
        if (FAILED(graphics.device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&replacementHeap))))
            return false;
        textures_.swap(replacements);
        rtvHeap_.Swap(replacementHeap);
        const UINT stride = graphics.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (size_t i = 0; i < textures_.size(); ++i) {
            rtvs_[i] = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
            rtvs_[i].ptr += i * stride;
            D3D12_RENDER_TARGET_VIEW_DESC view{};
            view.Format = kFormat;
            view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            // SSR 単独時は F0/F90 の実画像を持たず、null RTV への書込みを破棄する。
            graphics.device->CreateRenderTargetView(textures_[i].Get(), &view, rtvs_[i]);
        }
        textures_[0]->SetName(L"HIKARI.SceneSurface.ReflectionFallbackSpecular");
        textures_[1]->SetName(L"HIKARI.SceneSurface.ReflectionSpecularResponse");
        if (textures_[2])
            textures_[2]->SetName(L"HIKARI.SceneSurface.ReflectionMaterial");
        if (textures_[3])
            textures_[3]->SetName(L"HIKARI.SceneSurface.SubsurfaceDiffuse");
        workingSetBytes_ = bytes * count;
        width_ = width;
        height_ = height;
        NUMERIC::AdvanceNonZero(generation_);
    }
    ready_ = PublishDescriptors(graphics);
    return ready_;
}

bool SceneReflectionBuffer::PublishDescriptors(const GFX::Context& graphics) {
    auto& views = srvs_[frameSlot_];
    for (size_t i = 0; i < textures_.size(); ++i) {
        if (!textures_[i])
            continue;
        auto& view = views[i];
        if (!view.IsValid())
            view = AllocateRenderResourceDescriptor();
        if (!view.IsValid())
            return false;
    }
    if (descriptorGenerations_[frameSlot_] != generation_) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = kFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        for (size_t i = 0; i < textures_.size(); ++i)
            if (textures_[i])
                graphics.device->CreateShaderResourceView(textures_[i].Get(), &srv, views[i].cpu);
        descriptorGenerations_[frameSlot_] = generation_;
    }
    return true;
}

void SceneReflectionBuffer::Discard(bool releaseDescriptors, bool completed) {
    const auto& current = GFX::GetCurrentContext();
    const bool compatible = current.device == context_.device &&
                            current.deviceGeneration == context_.deviceGeneration &&
                            current.srvHeap == context_.srvHeap;
    const GFX::Context retireContext = compatible ? context_ : GFX::Context{};
    for (auto& resource : textures_) {
        if (completed)
            resource.Reset();
        else
            GFX::RetireD3D12ObjectForFrame(resource, retireContext, "SceneReflectionBuffer.Texture");
    }
    if (completed)
        rtvHeap_.Reset();
    else
        GFX::RetireD3D12ObjectForFrame(rtvHeap_, retireContext, "SceneReflectionBuffer.RTVHeap");
    if (releaseDescriptors) {
        for (auto& frame : srvs_)
            for (auto& view : frame) {
                if (compatible)
                    RetireRenderResourceDescriptor(view, retireContext, "SceneReflectionBuffer.SRV");
                else
                    view = {}; // 古いデバイスの記述子を新しい arena に返してはいけない。
            }
        descriptorGenerations_.fill(0);
    }
    rtvs_ = {};
    width_ = height_ = 0;
    lastRetireFence_ = workingSetBytes_ = 0;
    state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    ready_ = written_ = false;
}

void SceneReflectionBuffer::Release() {
    Discard(true);
    context_ = {};
    generation_ = 0;
    frameSlot_ = 0;
}

void SceneReflectionBuffer::Transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES nextState) {
    if (state_ == nextState)
        return;
    std::array<D3D12_RESOURCE_BARRIER, kTargetCount> barriers;
    UINT count = 0;
    for (const auto& texture : textures_)
        if (texture)
            barriers[count++] = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), state_, nextState);
    commands->ResourceBarrier(count, barriers.data());
    state_ = nextState;
}

bool SceneReflectionBuffer::BeginWrite(ID3D12GraphicsCommandList* commands) {
    written_ = false;
    if (!commands || !ready_)
        return false;
    lastRetireFence_ = context_.currentFrameRetireFenceValue;
    Transition(commands, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const float clear[]{0, 0, 0, 0};
    for (size_t i = 0; i < textures_.size(); ++i)
        if (textures_[i])
            commands->ClearRenderTargetView(rtvs_[i], clear, 0, nullptr);
    return true;
}

void SceneReflectionBuffer::EndWrite(ID3D12GraphicsCommandList* commands, bool succeeded) {
    if (!commands || !ready_ || state_ != D3D12_RESOURCE_STATE_RENDER_TARGET)
        return;
    Transition(commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    written_ = succeeded;
}

RenderTextureView SceneReflectionBuffer::GetView(size_t index) const {
    return {textures_[index].Get(),
            srvs_[frameSlot_][index].gpu,
            kFormat,
            width_,
            height_,
            1,
            state_,
            bool(textures_[index]) && ready_ && written_ && descriptorGenerations_[frameSlot_] == generation_};
}

} // namespace HIKARI::RENDER3D::SURFACE
