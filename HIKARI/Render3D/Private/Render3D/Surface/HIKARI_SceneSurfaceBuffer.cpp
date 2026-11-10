#include "Render3D/Surface/HIKARI_SceneSurfaceBuffer.h"
#include "Core/Numeric/HIKARI_NonZero.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>

#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::SURFACE {

bool SceneSurfaceBuffer::EnsureSize(const GFX::Context& graphics, uint32_t width, uint32_t height,
                                    bool enableFeatureMask) {
    width = (std::max)(1u, width);
    height = (std::max)(1u, height);
    const bool contextChanged = lastGraphicsContext_.deviceGeneration != 0u && graphics.deviceGeneration != 0u &&
                                    lastGraphicsContext_.deviceGeneration != graphics.deviceGeneration ||
                                lastGraphicsContext_.device != nullptr && graphics.device != nullptr &&
                                    lastGraphicsContext_.device != graphics.device;
    if (contextChanged) {
        ResetResources(false);
    }
    activeFrameSlot_ = graphics.frameIndex % GFX::kFrameResourceCount;
    lastGraphicsContext_ = graphics;
    if (packedSurface_ != nullptr && packedSurfaceRtv_.ptr != 0u && width_ == width && height_ == height) {
        return PublishPackedSurfaceSrv(graphics) && EnsureFeatureMask(graphics, enableFeatureMask);
    }

    ResetResources(false);
    return CreateResources(graphics, width, height, enableFeatureMask);
}

bool SceneSurfaceBuffer::SetFeatureMaskEnabled(const GFX::Context& graphics, bool enabled) {
    const bool contextChanged = lastGraphicsContext_.deviceGeneration != 0u && graphics.deviceGeneration != 0u &&
                                    lastGraphicsContext_.deviceGeneration != graphics.deviceGeneration ||
                                lastGraphicsContext_.device != nullptr && graphics.device != nullptr &&
                                    lastGraphicsContext_.device != graphics.device;
    if (contextChanged) {
        ResetResources(false);
    }
    activeFrameSlot_ = graphics.frameIndex % GFX::kFrameResourceCount;
    lastGraphicsContext_ = graphics;
    return EnsureFeatureMask(graphics, enabled);
}

void SceneSurfaceBuffer::ResetResources(bool releaseDescriptors) {
    const GFX::Context current = GFX::GetCurrentContext();
    const bool contextUnavailable = current.device == nullptr || current.deviceGeneration == 0u;
    const bool contextChanged =
        contextUnavailable ? lastGraphicsContext_.device != nullptr
                           : (lastGraphicsContext_.deviceGeneration != 0u &&
                                  lastGraphicsContext_.deviceGeneration != current.deviceGeneration ||
                              lastGraphicsContext_.device != nullptr && lastGraphicsContext_.device != current.device);
    const GFX::Context context =
        contextChanged ? GFX::Context{}
        : lastGraphicsContext_.device != nullptr || lastGraphicsContext_.deferredReleaseQueue != nullptr
            ? lastGraphicsContext_
            : current;
    GFX::RetireD3D12ObjectForFrame(packedSurface_, context, "SceneSurfaceBuffer.PackedSurface");
    GFX::RetireD3D12ObjectForFrame(featureMask_, context, "SceneSurfaceBuffer.FeatureMask");
    GFX::RetireD3D12ObjectForFrame(stylizedOutlineStyle_, context, "SceneSurfaceBuffer.StylizedOutlineStyle");
    GFX::RetireD3D12ObjectForFrame(rtvHeap_, context, "SceneSurfaceBuffer.RTVHeap");
    if (releaseDescriptors) {
        for (RenderResourceView& view : packedSurfaceSrvs_) {
            RetireRenderResourceDescriptor(view, context, "SceneSurfaceBuffer.PackedSurfaceSRV");
        }
        for (RenderResourceView& view : featureMaskSrvs_) {
            RetireRenderResourceDescriptor(view, context, "SceneSurfaceBuffer.FeatureMaskSRV");
        }
        for (RenderResourceView& view : stylizedOutlineStyleSrvs_) {
            RetireRenderResourceDescriptor(view, context, "SceneSurfaceBuffer.OutlineStyleSRV");
        }
        packedSurfaceSrvGenerations_.fill(0u);
        featureMaskSrvGenerations_.fill(0u);
        stylizedOutlineStyleSrvGenerations_.fill(0u);
    }
    packedSurfaceRtv_ = {};
    featureMaskRtv_ = {};
    stylizedOutlineStyleRtv_ = {};
    rtvDescriptorSize_ = 0u;
    width_ = 0;
    height_ = 0;
    packedSurfaceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    featureMaskState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    stylizedOutlineStyleState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void SceneSurfaceBuffer::Release() {
    ResetResources(true);
    packedSurfaceGeneration_ = 0u;
    featureMaskGeneration_ = 0u;
    stylizedOutlineStyleGeneration_ = 0u;
    activeFrameSlot_ = 0u;
    lastGraphicsContext_ = {};
}

void SceneSurfaceBuffer::DiscardResources() { ResetResources(false); }

bool SceneSurfaceBuffer::CreateResources(const GFX::Context& graphics, uint32_t width, uint32_t height,
                                         bool enableFeatureMask) {
    ID3D12Device* device = graphics.device;
    ID3D12DescriptorHeap* srvHeap = graphics.srvHeap;
    if (device == nullptr || srvHeap == nullptr) {
        return false;
    }

    const D3D12_CLEAR_VALUE clearValue{kNormalRoughnessFormat, {0.5f, 0.5f, 1.0f, 0.0f}};

    const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(kNormalRoughnessFormat, width, height, 1, 1, 1, 0,
                                                      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    HRESULT hr = GFX::VIDEO_MEMORY::CreateCommittedResource(
        device, &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        GFX::VIDEO_MEMORY::Owner::SceneTargets, IID_PPV_ARGS(packedSurface_.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "SceneSurfaceBuffer::Create packed surface")) {
        DEBUGLOG::PushRenderError("[SceneSurfaceBuffer][ERROR] packed surface resource creation failed.");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 3;
    hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "SceneSurfaceBuffer::Create RTV heap")) {
        return false;
    }

    packedSurfaceRtv_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    featureMaskRtv_ = packedSurfaceRtv_;
    featureMaskRtv_.ptr += rtvDescriptorSize_;
    stylizedOutlineStyleRtv_ = featureMaskRtv_;
    stylizedOutlineStyleRtv_.ptr += rtvDescriptorSize_;
    device->CreateRenderTargetView(packedSurface_.Get(), nullptr, packedSurfaceRtv_);

    width_ = width;
    height_ = height;
    packedSurfaceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    NUMERIC::AdvanceNonZero(packedSurfaceGeneration_);
    packedSurface_->SetName(L"HIKARI.SceneSurface.PackedNormalRoughnessMetallic");
    if (!PublishPackedSurfaceSrv(graphics) || !EnsureFeatureMask(graphics, enableFeatureMask)) {
        return false;
    }
    HIKARI_LOG_INFO("[SceneSurfaceBuffer] resized packed surface " + std::to_string(width_) + "x" +
                    std::to_string(height_));
    return true;
}

bool SceneSurfaceBuffer::EnsureFeatureMask(const GFX::Context& graphics, bool enabled) {
    if (!enabled) {
        if (featureMask_ != nullptr) {
            GFX::RetireD3D12ObjectForCurrentFrame(featureMask_, "SceneSurfaceBuffer.FeatureMask");
            featureMaskState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            NUMERIC::AdvanceNonZero(featureMaskGeneration_);
        }
        if (stylizedOutlineStyle_ != nullptr) {
            GFX::RetireD3D12ObjectForCurrentFrame(stylizedOutlineStyle_, "SceneSurfaceBuffer.StylizedOutlineStyle");
            stylizedOutlineStyleState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            NUMERIC::AdvanceNonZero(stylizedOutlineStyleGeneration_);
        }
        return true;
    }
    if (featureMask_ != nullptr && stylizedOutlineStyle_ != nullptr) {
        return PublishFeatureMaskSrv(graphics) && PublishStylizedOutlineStyleSrv(graphics);
    }
    if (featureMask_ != nullptr || stylizedOutlineStyle_ != nullptr) {
        GFX::RetireD3D12ObjectForCurrentFrame(featureMask_, "SceneSurfaceBuffer.FeatureMask");
        GFX::RetireD3D12ObjectForCurrentFrame(stylizedOutlineStyle_, "SceneSurfaceBuffer.StylizedOutlineStyle");
        featureMaskState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        stylizedOutlineStyleState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ++featureMaskGeneration_;
        ++stylizedOutlineStyleGeneration_;
    }
    if (graphics.device == nullptr || graphics.srvHeap == nullptr || width_ == 0u || height_ == 0u ||
        featureMaskRtv_.ptr == 0u || stylizedOutlineStyleRtv_.ptr == 0u) {
        return false;
    }

    const D3D12_CLEAR_VALUE clearValue{kFeatureMaskFormat, {0.0f, 0.0f, 0.0f, 0.0f}};
    const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto textureDescription = CD3DX12_RESOURCE_DESC::Tex2D(kFeatureMaskFormat, width_, height_, 1, 1, 1, 0,
                                                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    Microsoft::WRL::ComPtr<ID3D12Resource> featureMaskReplacement{};
    if (!HIKARI_DX_CHECK(
            GFX::VIDEO_MEMORY::CreateCommittedResource(graphics.device, &heapProps, D3D12_HEAP_FLAG_NONE,
                                                       &textureDescription, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                       &clearValue, GFX::VIDEO_MEMORY::Owner::SceneTargets,
                                                       IID_PPV_ARGS(featureMaskReplacement.GetAddressOf())),
            "SceneSurfaceBuffer::Create feature mask")) {
        return false;
    }

    const D3D12_CLEAR_VALUE outlineStyleClearValue{kStylizedOutlineStyleFormat, {0.025f, 0.030f, 0.045f, 0.25f}};
    const auto outlineStyleDescription = CD3DX12_RESOURCE_DESC::Tex2D(kStylizedOutlineStyleFormat, width_, height_, 1,
                                                                      1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    Microsoft::WRL::ComPtr<ID3D12Resource> stylizedOutlineStyleReplacement{};
    if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                             graphics.device, &heapProps, D3D12_HEAP_FLAG_NONE, &outlineStyleDescription,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &outlineStyleClearValue,
                             GFX::VIDEO_MEMORY::Owner::SceneTargets,
                             IID_PPV_ARGS(stylizedOutlineStyleReplacement.GetAddressOf())),
                         "SceneSurfaceBuffer::Create stylized outline style")) {
        return false;
    }
    featureMask_.Swap(featureMaskReplacement);
    stylizedOutlineStyle_.Swap(stylizedOutlineStyleReplacement);
    graphics.device->CreateRenderTargetView(featureMask_.Get(), nullptr, featureMaskRtv_);
    graphics.device->CreateRenderTargetView(stylizedOutlineStyle_.Get(), nullptr, stylizedOutlineStyleRtv_);
    featureMaskState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    stylizedOutlineStyleState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    NUMERIC::AdvanceNonZero(featureMaskGeneration_);
    NUMERIC::AdvanceNonZero(stylizedOutlineStyleGeneration_);
    featureMask_->SetName(L"HIKARI.SceneSurface.StylizedFeatureMask");
    stylizedOutlineStyle_->SetName(L"HIKARI.SceneSurface.StylizedOutlineStyle");
    return PublishFeatureMaskSrv(graphics) && PublishStylizedOutlineStyleSrv(graphics);
}

bool SceneSurfaceBuffer::PublishPackedSurfaceSrv(const GFX::Context& graphics) {
    if (packedSurface_ == nullptr || graphics.device == nullptr) {
        return false;
    }
    UpdateRenderResourceDescriptorPoolContext(graphics);
    RenderResourceView& view = packedSurfaceSrvs_[activeFrameSlot_];
    if (!view.IsValid()) {
        view = AllocateRenderResourceDescriptor();
    }
    if (!view.IsValid()) {
        return false;
    }
    if (packedSurfaceSrvGenerations_[activeFrameSlot_] != packedSurfaceGeneration_) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = kNormalRoughnessFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1u;
        graphics.device->CreateShaderResourceView(packedSurface_.Get(), &srv, view.cpu);
        packedSurfaceSrvGenerations_[activeFrameSlot_] = packedSurfaceGeneration_;
    }
    return true;
}

bool SceneSurfaceBuffer::PublishFeatureMaskSrv(const GFX::Context& graphics) {
    if (featureMask_ == nullptr || graphics.device == nullptr) {
        return false;
    }
    UpdateRenderResourceDescriptorPoolContext(graphics);
    RenderResourceView& view = featureMaskSrvs_[activeFrameSlot_];
    if (!view.IsValid()) {
        view = AllocateRenderResourceDescriptor();
    }
    if (!view.IsValid()) {
        return false;
    }
    if (featureMaskSrvGenerations_[activeFrameSlot_] != featureMaskGeneration_) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = kFeatureMaskFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1u;
        graphics.device->CreateShaderResourceView(featureMask_.Get(), &srv, view.cpu);
        featureMaskSrvGenerations_[activeFrameSlot_] = featureMaskGeneration_;
    }
    return true;
}

bool SceneSurfaceBuffer::PublishStylizedOutlineStyleSrv(const GFX::Context& graphics) {
    if (stylizedOutlineStyle_ == nullptr || graphics.device == nullptr) {
        return false;
    }
    UpdateRenderResourceDescriptorPoolContext(graphics);
    RenderResourceView& view = stylizedOutlineStyleSrvs_[activeFrameSlot_];
    if (!view.IsValid()) {
        view = AllocateRenderResourceDescriptor();
    }
    if (!view.IsValid()) {
        return false;
    }
    if (stylizedOutlineStyleSrvGenerations_[activeFrameSlot_] != stylizedOutlineStyleGeneration_) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = kStylizedOutlineStyleFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1u;
        graphics.device->CreateShaderResourceView(stylizedOutlineStyle_.Get(), &srv, view.cpu);
        stylizedOutlineStyleSrvGenerations_[activeFrameSlot_] = stylizedOutlineStyleGeneration_;
    }
    return true;
}

void SceneSurfaceBuffer::BeginWrite(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE depthDsv) {
    if (cmd == nullptr || !IsValid()) {
        return;
    }
    TransitionToRenderTargets(cmd);
    const float clear[] = {0.5f, 0.5f, 1.0f, 0.0f};
    cmd->ClearRenderTargetView(packedSurfaceRtv_, clear, 0, nullptr);
    const float maskClear[] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (HasFeatureMask()) {
        cmd->ClearRenderTargetView(featureMaskRtv_, maskClear, 0, nullptr);
    }
    const float outlineStyleClear[]{0.025f, 0.030f, 0.045f, 0.25f};
    if (HasStylizedOutlineStyle()) {
        cmd->ClearRenderTargetView(stylizedOutlineStyleRtv_, outlineStyleClear, 0, nullptr);
    }
    BindRenderTargets(cmd, depthDsv);
}

void SceneSurfaceBuffer::ResumeWrite(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE depthDsv) {
    if (cmd == nullptr || !IsValid()) {
        return;
    }
    TransitionToRenderTargets(cmd);
    BindRenderTargets(cmd, depthDsv);
}

void SceneSurfaceBuffer::TransitionToRenderTargets(ID3D12GraphicsCommandList* cmd) {
    if (packedSurfaceState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(packedSurface_.Get(), packedSurfaceState_,
                                                            D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->ResourceBarrier(1, &barrier);
        packedSurfaceState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    if (HasFeatureMask() && featureMaskState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(featureMask_.Get(), featureMaskState_,
                                                            D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->ResourceBarrier(1, &barrier);
        featureMaskState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    if (HasStylizedOutlineStyle() && stylizedOutlineStyleState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(stylizedOutlineStyle_.Get(), stylizedOutlineStyleState_,
                                                            D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->ResourceBarrier(1, &barrier);
        stylizedOutlineStyleState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
}

void SceneSurfaceBuffer::BindRenderTargets(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE depthDsv) {
    const D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[]{packedSurfaceRtv_, featureMaskRtv_, stylizedOutlineStyleRtv_};
    const bool writeStylizedSurfaces = HasFeatureMask() && HasStylizedOutlineStyle();
    cmd->OMSetRenderTargets(writeStylizedSurfaces ? 3u : 1u, renderTargets, FALSE,
                            depthDsv.ptr != 0 ? &depthDsv : nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
}

void SceneSurfaceBuffer::EndWrite(ID3D12GraphicsCommandList* cmd) {
    if (cmd == nullptr || !IsValid()) {
        return;
    }

    if (packedSurfaceState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(packedSurface_.Get(), packedSurfaceState_,
                                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd->ResourceBarrier(1, &barrier);
        packedSurfaceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (HasFeatureMask() && featureMaskState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(featureMask_.Get(), featureMaskState_,
                                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd->ResourceBarrier(1, &barrier);
        featureMaskState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (HasStylizedOutlineStyle() && stylizedOutlineStyleState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(stylizedOutlineStyle_.Get(), stylizedOutlineStyleState_,
                                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd->ResourceBarrier(1, &barrier);
        stylizedOutlineStyleState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}

bool SceneSurfaceBuffer::IsValid() const {
    return packedSurface_ != nullptr && packedSurfaceSrvs_[activeFrameSlot_].IsValid() &&
           packedSurfaceSrvGenerations_[activeFrameSlot_] == packedSurfaceGeneration_ && packedSurfaceRtv_.ptr != 0;
}

RenderTextureView SceneSurfaceBuffer::GetPackedSurfaceView() const {
    return {packedSurface_.Get(),
            packedSurfaceSrvs_[activeFrameSlot_].gpu,
            kNormalRoughnessFormat,
            width_,
            height_,
            1,
            packedSurfaceState_,
            IsValid()};
}

bool SceneSurfaceBuffer::HasFeatureMask() const {
    return featureMask_ != nullptr && featureMaskSrvs_[activeFrameSlot_].IsValid() &&
           featureMaskSrvGenerations_[activeFrameSlot_] == featureMaskGeneration_ && featureMaskRtv_.ptr != 0u;
}

RenderTextureView SceneSurfaceBuffer::GetFeatureMaskView() const {
    return {featureMask_.Get(),
            featureMaskSrvs_[activeFrameSlot_].gpu,
            kFeatureMaskFormat,
            width_,
            height_,
            1,
            featureMaskState_,
            HasFeatureMask()};
}

bool SceneSurfaceBuffer::HasStylizedOutlineStyle() const {
    return stylizedOutlineStyle_ != nullptr && stylizedOutlineStyleSrvs_[activeFrameSlot_].IsValid() &&
           stylizedOutlineStyleSrvGenerations_[activeFrameSlot_] == stylizedOutlineStyleGeneration_ &&
           stylizedOutlineStyleRtv_.ptr != 0u;
}

RenderTextureView SceneSurfaceBuffer::GetStylizedOutlineStyleView() const {
    return {stylizedOutlineStyle_.Get(),
            stylizedOutlineStyleSrvs_[activeFrameSlot_].gpu,
            kStylizedOutlineStyleFormat,
            width_,
            height_,
            1,
            stylizedOutlineStyleState_,
            HasStylizedOutlineStyle()};
}

} // namespace HIKARI::RENDER3D::SURFACE
