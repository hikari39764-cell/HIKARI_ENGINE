#include "Render3D/ScreenSpace/HIKARI_ScreenSpaceGeometryAux.h"

#include <algorithm>

#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "HIKARI_Services.h"

namespace HIKARI::RENDER3D::SCREENSPACE {

    namespace {
    }

    bool ScreenSpaceGeometryAux::EnsureSize(uint32_t width, uint32_t height) {
        width = std::max(1u, width);
        height = std::max(1u, height);
        if (IsValid() && width_ == width && height_ == height) {
            return true;
        }

        Release();
        return CreateResources(width, height);
    }

    void ScreenSpaceGeometryAux::Release() {
        GFX::RetireD3D12ObjectForCurrentFrame(normalRoughness_, "ScreenSpaceGeometryAux.NormalRoughness");
        GFX::RetireD3D12ObjectForCurrentFrame(rtvHeap_, "ScreenSpaceGeometryAux.RTVHeap");
        normalRoughnessRtv_ = {};
        normalRoughnessSrvCpu_ = {};
        normalRoughnessSrvGpu_ = {};
        width_ = 0;
        height_ = 0;
        normalRoughnessState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    bool ScreenSpaceGeometryAux::CreateResources(uint32_t width, uint32_t height) {
        ID3D12Device* device = SERVICES::gCtx.device;
        ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
        if (device == nullptr || srvHeap == nullptr) {
            return false;
        }

        const D3D12_CLEAR_VALUE clearValue{
            kNormalRoughnessFormat,
            { 0.5f, 0.5f, 1.0f, 1.0f }
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
            IID_PPV_ARGS(normalRoughness_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ScreenSpaceGeometryAux::Create normal/roughness")) {
            DEBUGLOG::PushRenderError("[ScreenSpaceGeometryAux][ERROR] normal/roughness resource creation failed.");
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 1;
        hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ScreenSpaceGeometryAux::Create RTV heap")) {
            return false;
        }

        normalRoughnessRtv_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(normalRoughness_.Get(), nullptr, normalRoughnessRtv_);

        const UINT descriptorSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT srvIndex = GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::SceneNormalRoughness);
        normalRoughnessSrvCpu_ = GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, srvIndex);
        normalRoughnessSrvGpu_ = GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, srvIndex);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = kNormalRoughnessFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(normalRoughness_.Get(), &srv, normalRoughnessSrvCpu_);

        width_ = width;
        height_ = height;
        normalRoughnessState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        normalRoughness_->SetName(L"HIKARI.SceneNormalRoughness");
        HIKARI_LOG_INFO("[ScreenSpaceGeometryAux] resized normal/roughness " + std::to_string(width_) + "x" + std::to_string(height_));
        return true;
    }

    void ScreenSpaceGeometryAux::BeginNormalRoughnessPass(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE depthDsv) {
        if (cmd == nullptr || !IsValid()) {
            return;
        }

        if (normalRoughnessState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                normalRoughness_.Get(),
                normalRoughnessState_,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd->ResourceBarrier(1, &barrier);
            normalRoughnessState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        const float clear[] = { 0.5f, 0.5f, 1.0f, 1.0f };
        cmd->ClearRenderTargetView(normalRoughnessRtv_, clear, 0, nullptr);
        cmd->OMSetRenderTargets(1, &normalRoughnessRtv_, FALSE, depthDsv.ptr != 0 ? &depthDsv : nullptr);

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width_);
        viewport.Height = static_cast<float>(height_);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);
    }

    void ScreenSpaceGeometryAux::EndNormalRoughnessPass(ID3D12GraphicsCommandList* cmd) {
        if (cmd == nullptr || !IsValid()) {
            return;
        }

        if (normalRoughnessState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                normalRoughness_.Get(),
                normalRoughnessState_,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &barrier);
            normalRoughnessState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }

    bool ScreenSpaceGeometryAux::IsValid() const {
        return normalRoughness_ != nullptr && normalRoughnessSrvGpu_.ptr != 0 && normalRoughnessRtv_.ptr != 0;
    }

} // namespace HIKARI::RENDER3D::SCREENSPACE
