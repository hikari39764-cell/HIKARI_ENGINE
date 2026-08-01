#include "Render3D/Surface/HIKARI_SceneSurfaceBuffer.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>

#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::SURFACE {

    bool SceneSurfaceBuffer::EnsureSize(
        const GFX::Context& graphics,
        uint32_t width,
        uint32_t height,
        bool enableFeatureMask) {
        width = (std::max)(1u, width);
        height = (std::max)(1u, height);
        activeFrameSlot_ =
            graphics.frameIndex % GFX::kFrameResourceCount;
        if (packedSurface_ != nullptr && packedSurfaceRtv_.ptr != 0u &&
            width_ == width && height_ == height) {
            return PublishPackedSurfaceSrv(graphics) &&
                EnsureFeatureMask(graphics, enableFeatureMask);
        }

        ResetResources(false);
        return CreateResources(
            graphics, width, height, enableFeatureMask);
    }

    bool SceneSurfaceBuffer::SetFeatureMaskEnabled(
        const GFX::Context& graphics,
        bool enabled) {

        activeFrameSlot_ =
            graphics.frameIndex % GFX::kFrameResourceCount;
        return EnsureFeatureMask(graphics, enabled);
    }

    void SceneSurfaceBuffer::ResetResources(bool releaseDescriptors) {
        GFX::RetireD3D12ObjectForCurrentFrame(packedSurface_, "SceneSurfaceBuffer.PackedSurface");
        GFX::RetireD3D12ObjectForCurrentFrame(featureMask_, "SceneSurfaceBuffer.FeatureMask");
        GFX::RetireD3D12ObjectForCurrentFrame(rtvHeap_, "SceneSurfaceBuffer.RTVHeap");
        if (releaseDescriptors) {
            for (RenderResourceView& view : packedSurfaceSrvs_) {
                if (view.IsValid()) {
                    (void)ReleaseRenderResourceDescriptor(view);
                    view = {};
                }
            }
            for (RenderResourceView& view : featureMaskSrvs_) {
                if (view.IsValid()) {
                    (void)ReleaseRenderResourceDescriptor(view);
                    view = {};
                }
            }
            packedSurfaceSrvGenerations_.fill(0u);
            featureMaskSrvGenerations_.fill(0u);
        }
        packedSurfaceRtv_ = {};
        featureMaskRtv_ = {};
        rtvDescriptorSize_ = 0u;
        width_ = 0;
        height_ = 0;
        packedSurfaceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        featureMaskState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    void SceneSurfaceBuffer::Release() {
        ResetResources(true);
        packedSurfaceGeneration_ = 0u;
        featureMaskGeneration_ = 0u;
        activeFrameSlot_ = 0u;
    }

    void SceneSurfaceBuffer::DiscardResources() {
        ResetResources(false);
    }

    bool SceneSurfaceBuffer::CreateResources(
        const GFX::Context& graphics,
        uint32_t width,
        uint32_t height,
        bool enableFeatureMask) {

        ID3D12Device* device = graphics.device;
        ID3D12DescriptorHeap* srvHeap = graphics.srvHeap;
        if (device == nullptr || srvHeap == nullptr) {
            return false;
        }

        const D3D12_CLEAR_VALUE clearValue{
            kNormalRoughnessFormat,
            { 0.5f, 0.5f, 1.0f, 0.0f }
        };

        const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            kNormalRoughnessFormat,
            width,
            height,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue,
            IID_PPV_ARGS(packedSurface_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "SceneSurfaceBuffer::Create packed surface")) {
            DEBUGLOG::PushRenderError("[SceneSurfaceBuffer][ERROR] packed surface resource creation failed.");
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 2;
        hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "SceneSurfaceBuffer::Create RTV heap")) {
            return false;
        }

        packedSurfaceRtv_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        rtvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        featureMaskRtv_ = packedSurfaceRtv_;
        featureMaskRtv_.ptr += rtvDescriptorSize_;
        device->CreateRenderTargetView(
            packedSurface_.Get(),
            nullptr,
            packedSurfaceRtv_);

        width_ = width;
        height_ = height;
        packedSurfaceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ++packedSurfaceGeneration_;
        if (packedSurfaceGeneration_ == 0u) {
            packedSurfaceGeneration_ = 1u;
        }
        packedSurface_->SetName(L"HIKARI.SceneSurface.PackedNormalRoughnessMetallic");
        if (!PublishPackedSurfaceSrv(graphics) ||
            !EnsureFeatureMask(graphics, enableFeatureMask)) {
            return false;
        }
        HIKARI_LOG_INFO("[SceneSurfaceBuffer] resized packed surface " + std::to_string(width_) + "x" + std::to_string(height_));
        return true;
    }

    bool SceneSurfaceBuffer::EnsureFeatureMask(
        const GFX::Context& graphics,
        bool enabled) {

        if (!enabled) {
            if (featureMask_ != nullptr) {
                GFX::RetireD3D12ObjectForCurrentFrame(
                    featureMask_, "SceneSurfaceBuffer.FeatureMask");
                featureMaskState_ =
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                ++featureMaskGeneration_;
                if (featureMaskGeneration_ == 0u) {
                    featureMaskGeneration_ = 1u;
                }
            }
            return true;
        }
        if (featureMask_ != nullptr) {
            return PublishFeatureMaskSrv(graphics);
        }
        if (graphics.device == nullptr || graphics.srvHeap == nullptr ||
            width_ == 0u || height_ == 0u || featureMaskRtv_.ptr == 0u) {
            return false;
        }

        const D3D12_CLEAR_VALUE clearValue{
            kFeatureMaskFormat,
            { 0.0f, 0.0f, 0.0f, 0.0f }
        };
        const auto heapProps =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto textureDescription = CD3DX12_RESOURCE_DESC::Tex2D(
            kFeatureMaskFormat,
            width_,
            height_,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        if (!HIKARI_DX_CHECK(
                graphics.device->CreateCommittedResource(
                    &heapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &textureDescription,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    &clearValue,
                    IID_PPV_ARGS(featureMask_.GetAddressOf())),
                "SceneSurfaceBuffer::Create feature mask")) {
            return false;
        }
        graphics.device->CreateRenderTargetView(
            featureMask_.Get(), nullptr, featureMaskRtv_);
        featureMaskState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ++featureMaskGeneration_;
        if (featureMaskGeneration_ == 0u) {
            featureMaskGeneration_ = 1u;
        }
        featureMask_->SetName(L"HIKARI.SceneSurface.StylizedFeatureMask");
        return PublishFeatureMaskSrv(graphics);
    }

    bool SceneSurfaceBuffer::PublishPackedSurfaceSrv(
        const GFX::Context& graphics) {

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
        if (packedSurfaceSrvGenerations_[activeFrameSlot_] !=
            packedSurfaceGeneration_) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = kNormalRoughnessFormat;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1u;
            graphics.device->CreateShaderResourceView(
                packedSurface_.Get(), &srv, view.cpu);
            packedSurfaceSrvGenerations_[activeFrameSlot_] =
                packedSurfaceGeneration_;
        }
        return true;
    }

    bool SceneSurfaceBuffer::PublishFeatureMaskSrv(
        const GFX::Context& graphics) {

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
        if (featureMaskSrvGenerations_[activeFrameSlot_] !=
            featureMaskGeneration_) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = kFeatureMaskFormat;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1u;
            graphics.device->CreateShaderResourceView(
                featureMask_.Get(), &srv, view.cpu);
            featureMaskSrvGenerations_[activeFrameSlot_] =
                featureMaskGeneration_;
        }
        return true;
    }

    void SceneSurfaceBuffer::BeginWrite(
        ID3D12GraphicsCommandList* cmd,
        D3D12_CPU_DESCRIPTOR_HANDLE depthDsv) {
        if (cmd == nullptr || !IsValid()) {
            return;
        }

        if (packedSurfaceState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                packedSurface_.Get(),
                packedSurfaceState_,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd->ResourceBarrier(1, &barrier);
            packedSurfaceState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        if (HasFeatureMask() &&
            featureMaskState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                featureMask_.Get(),
                featureMaskState_,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd->ResourceBarrier(1, &barrier);
            featureMaskState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        const float clear[] = { 0.5f, 0.5f, 1.0f, 0.0f };
        cmd->ClearRenderTargetView(packedSurfaceRtv_, clear, 0, nullptr);
        const float maskClear[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (HasFeatureMask()) {
            cmd->ClearRenderTargetView(
                featureMaskRtv_, maskClear, 0, nullptr);
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[]{
            packedSurfaceRtv_, featureMaskRtv_
        };
        cmd->OMSetRenderTargets(
            HasFeatureMask() ? 2u : 1u,
            renderTargets,
            FALSE,
            depthDsv.ptr != 0 ? &depthDsv : nullptr);

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width_);
        viewport.Height = static_cast<float>(height_);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);
    }

    void SceneSurfaceBuffer::EndWrite(ID3D12GraphicsCommandList* cmd) {
        if (cmd == nullptr || !IsValid()) {
            return;
        }

        if (packedSurfaceState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                packedSurface_.Get(),
                packedSurfaceState_,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &barrier);
            packedSurfaceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        if (HasFeatureMask() &&
            featureMaskState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                featureMask_.Get(),
                featureMaskState_,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &barrier);
            featureMaskState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }

    bool SceneSurfaceBuffer::IsValid() const {
        return packedSurface_ != nullptr &&
            packedSurfaceSrvs_[activeFrameSlot_].IsValid() &&
            packedSurfaceSrvGenerations_[activeFrameSlot_] ==
                packedSurfaceGeneration_ &&
            packedSurfaceRtv_.ptr != 0;
    }

    RenderTextureView SceneSurfaceBuffer::GetPackedSurfaceView() const {
        return {
            packedSurface_.Get(), packedSurfaceSrvs_[activeFrameSlot_].gpu,
            kNormalRoughnessFormat, width_, height_, 1,
            packedSurfaceState_, IsValid()
        };
    }

    bool SceneSurfaceBuffer::HasFeatureMask() const {
        return featureMask_ != nullptr &&
            featureMaskSrvs_[activeFrameSlot_].IsValid() &&
            featureMaskSrvGenerations_[activeFrameSlot_] ==
                featureMaskGeneration_ &&
            featureMaskRtv_.ptr != 0u;
    }

    RenderTextureView SceneSurfaceBuffer::GetFeatureMaskView() const {
        return {
            featureMask_.Get(), featureMaskSrvs_[activeFrameSlot_].gpu,
            kFeatureMaskFormat, width_, height_, 1,
            featureMaskState_, HasFeatureMask()
        };
    }

} // namespace HIKARI::RENDER3D::SURFACE
