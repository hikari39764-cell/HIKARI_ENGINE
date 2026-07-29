#include "Render3D/Surface/HIKARI_SceneSurfaceBuffer.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>

#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

namespace HIKARI::RENDER3D::SURFACE {

    bool SceneSurfaceBuffer::EnsureSize(
        const GFX::Context& graphics,
        uint32_t width,
        uint32_t height) {
        width = std::max(1u, width);
        height = std::max(1u, height);
        if (IsValid() && width_ == width && height_ == height) {
            return true;
        }

        Release();
        return CreateResources(graphics, width, height);
    }

    void SceneSurfaceBuffer::Release() {
        GFX::RetireD3D12ObjectForCurrentFrame(packedSurface_, "SceneSurfaceBuffer.PackedSurface");
        GFX::RetireD3D12ObjectForCurrentFrame(rtvHeap_, "SceneSurfaceBuffer.RTVHeap");
        packedSurfaceRtv_ = {};
        packedSurfaceSrvCpu_ = {};
        packedSurfaceSrvGpu_ = {};
        width_ = 0;
        height_ = 0;
        packedSurfaceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    bool SceneSurfaceBuffer::CreateResources(
        const GFX::Context& graphics,
        uint32_t width,
        uint32_t height) {

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
        rtvHeapDesc.NumDescriptors = 1;
        hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "SceneSurfaceBuffer::Create RTV heap")) {
            return false;
        }

        packedSurfaceRtv_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(
            packedSurface_.Get(),
            nullptr,
            packedSurfaceRtv_);

        const UINT descriptorSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT srvIndex =
            GFX::DESCRIPTOR::ToIndex(
                GFX::DESCRIPTOR::SystemSrv::SceneSurface);
        packedSurfaceSrvCpu_ =
            GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, srvIndex);
        packedSurfaceSrvGpu_ =
            GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, srvIndex);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = kNormalRoughnessFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(
            packedSurface_.Get(),
            &srv,
            packedSurfaceSrvCpu_);

        width_ = width;
        height_ = height;
        packedSurfaceState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        packedSurface_->SetName(L"HIKARI.SceneSurface.PackedNormalRoughnessMetallic");
        HIKARI_LOG_INFO("[SceneSurfaceBuffer] resized packed surface " + std::to_string(width_) + "x" + std::to_string(height_));
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

        const float clear[] = { 0.5f, 0.5f, 1.0f, 0.0f };
        cmd->ClearRenderTargetView(packedSurfaceRtv_, clear, 0, nullptr);
        cmd->OMSetRenderTargets(
            1,
            &packedSurfaceRtv_,
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
    }

    bool SceneSurfaceBuffer::IsValid() const {
        return packedSurface_ != nullptr &&
            packedSurfaceSrvGpu_.ptr != 0 &&
            packedSurfaceRtv_.ptr != 0;
    }

    RenderTextureView SceneSurfaceBuffer::GetPackedSurfaceView() const {
        return {
            packedSurface_.Get(), packedSurfaceSrvGpu_,
            kNormalRoughnessFormat, width_, height_, 1,
            packedSurfaceState_, IsValid()
        };
    }

} // namespace HIKARI::RENDER3D::SURFACE
