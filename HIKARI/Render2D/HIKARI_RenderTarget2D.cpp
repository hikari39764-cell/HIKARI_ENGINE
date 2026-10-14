#include "Render2D/HIKARI_RenderTarget2D.h"
#include <cassert>
#include <sstream>
#include <utility>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI {

bool RenderTarget2D::Init(int width, int height, DXGI_FORMAT format, bool withDepth,
                          const std::array<float, 4>& optimizedClearColor, bool publishDepthSrv,
                          bool allowUnorderedAccess) {
    if (initialized_) {
        return true;
    }

    width_ = width;
    height_ = height;
    format_ = format;
    hasDepth_ = withDepth;
    publishDepthSrv_ = publishDepthSrv;
    allowUnorderedAccess_ = allowUnorderedAccess;
    optimizedClearColor_ = optimizedClearColor;

    if (!CreateResources() || (publishDepthSrv_ && hasDepth_ && !EnsureDepthSrv())) {
        Finalize();
        return false;
    }

    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left = 0;
    scissorRect_.top = 0;
    scissorRect_.right = width_;
    scissorRect_.bottom = height_;

    initialized_ = true;
    return true;
}

void RenderTarget2D::UpdateContext(const HIKARI::GFX::Context& ctx) {
    const bool contextChanged = context_.device != ctx.device || context_.srvHeap != ctx.srvHeap ||
                                context_.deviceGeneration != ctx.deviceGeneration;
    if (contextChanged) {
        ReleaseDepthSrv();
    }
    context_ = ctx;
}

void RenderTarget2D::Swap(RenderTarget2D& other) noexcept {
    using std::swap;
    swap(colorTex_, other.colorTex_);
    swap(colorState_, other.colorState_);
    swap(depthTex_, other.depthTex_);
    swap(depthState_, other.depthState_);
    swap(rtvHeap_, other.rtvHeap_);
    swap(srvHeap_, other.srvHeap_);
    swap(dsvHeap_, other.dsvHeap_);
    swap(rtvHandle_, other.rtvHandle_);
    swap(srvCpuHandle_, other.srvCpuHandle_);
    swap(srvGpuHandle_, other.srvGpuHandle_);
    swap(dsvHandle_, other.dsvHandle_);
    swap(readOnlyDsvHandle_, other.readOnlyDsvHandle_);
    swap(depthSrv_, other.depthSrv_);
    swap(dsvDescriptorSize_, other.dsvDescriptorSize_);
    swap(viewport_, other.viewport_);
    swap(scissorRect_, other.scissorRect_);
    swap(width_, other.width_);
    swap(height_, other.height_);
    swap(format_, other.format_);
    swap(initialized_, other.initialized_);
    swap(hasDepth_, other.hasDepth_);
    swap(publishDepthSrv_, other.publishDepthSrv_);
    swap(allowUnorderedAccess_, other.allowUnorderedAccess_);
    swap(debugName_, other.debugName_);
    swap(context_, other.context_);
    swap(optimizedClearColor_, other.optimizedClearColor_);
}

void RenderTarget2D::ReleaseDepthSrv() noexcept {
    RENDER3D::RetireRenderResourceDescriptor(depthSrv_, context_, "RenderTarget2D.DepthSRV");
}

bool RenderTarget2D::EnsureDepthSrv() {
    if (!publishDepthSrv_ || !hasDepth_ || context_.device == nullptr || context_.srvHeap == nullptr ||
        depthTex_ == nullptr) {
        return false;
    }

    if (depthSrv_.IsValid()) {
        return true;
    }
    RENDER3D::UpdateRenderResourceDescriptorPoolContext(context_);
    depthSrv_ = RENDER3D::AllocateTexture2DSrvDescriptor(depthTex_.Get(), DXGI_FORMAT_R32_FLOAT);
    return depthSrv_.IsValid();
}

void RenderTarget2D::Finalize() {
    GFX::RetireD3D12ObjectForFrame(colorTex_, context_, "RenderTarget2D.Color");
    GFX::RetireD3D12ObjectForFrame(depthTex_, context_, "RenderTarget2D.Depth");
    GFX::RetireD3D12ObjectForFrame(rtvHeap_, context_, "RenderTarget2D.RTVHeap");
    GFX::RetireD3D12ObjectForFrame(srvHeap_, context_, "RenderTarget2D.SRVHeap");
    GFX::RetireD3D12ObjectForFrame(dsvHeap_, context_, "RenderTarget2D.DSVHeap");
    rtvHandle_ = {};
    srvCpuHandle_ = {};
    srvGpuHandle_ = {};
    dsvHandle_ = {};
    readOnlyDsvHandle_ = {};
    ReleaseDepthSrv();
    dsvDescriptorSize_ = 0;

    initialized_ = false;
    hasDepth_ = false;
    publishDepthSrv_ = false;
    allowUnorderedAccess_ = false;
}

void RenderTarget2D::SetDebugName(std::string name) {
    debugName_ = std::move(name);
    if (colorTex_) {
        const std::wstring colorName = GFX::Widen(debugName_ + ".Color");
        GFX::SetD3D12Name(colorTex_.Get(), colorName.c_str());
    }
    if (depthTex_) {
        const std::wstring depthName = GFX::Widen(debugName_ + ".Depth");
        GFX::SetD3D12Name(depthTex_.Get(), depthName.c_str());
    }
    if (rtvHeap_) {
        const std::wstring nameW = GFX::Widen(debugName_ + ".RTVHeap");
        GFX::SetD3D12Name(rtvHeap_.Get(), nameW.c_str());
    }
    if (srvHeap_) {
        const std::wstring nameW = GFX::Widen(debugName_ + ".SRVHeap");
        GFX::SetD3D12Name(srvHeap_.Get(), nameW.c_str());
    }
    if (dsvHeap_) {
        const std::wstring nameW = GFX::Widen(debugName_ + ".DSVHeap");
        GFX::SetD3D12Name(dsvHeap_.Get(), nameW.c_str());
    }
}

std::string RenderTarget2D::DumpState() const {
    std::ostringstream oss;
    oss << "[RenderTarget2D] name=" << debugName_ << " initialized=" << initialized_ << " size=" << width_ << "x"
        << height_ << " format=" << GFX::FormatToString(format_) << " hasDepth=" << hasDepth_
        << " allowUav=" << allowUnorderedAccess_ << " colorTex=" << (colorTex_ ? 1 : 0)
        << " depthTex=" << (depthTex_ ? 1 : 0) << " rtvHeap=" << (rtvHeap_ ? 1 : 0) << " srvHeap=" << (srvHeap_ ? 1 : 0)
        << " dsvHeap=" << (dsvHeap_ ? 1 : 0) << " srvGpu=0x" << std::hex << srvGpuHandle_.ptr << std::dec
        << " depthSrvGpu=0x" << std::hex << GetDepthSrvGpu().ptr << std::dec
        << " colorState=" << GFX::ResourceStateToString(colorState_)
        << " depthState=" << GFX::ResourceStateToString(depthState_);
    return oss.str();
}

bool RenderTarget2D::CreateResources() {
    ID3D12Device* device = context_.device;
    if (!device) {
        return false;
    }

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_FLAGS colorFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (allowUnorderedAccess_) {
        colorFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(format_, static_cast<UINT64>(width_),
                                                                 static_cast<UINT>(height_), 1, 1, 1, 0, colorFlags);

    D3D12_CLEAR_VALUE colorClearValue{};
    colorClearValue.Format = format_;
    colorClearValue.Color[0] = optimizedClearColor_[0];
    colorClearValue.Color[1] = optimizedClearColor_[1];
    colorClearValue.Color[2] = optimizedClearColor_[2];
    colorClearValue.Color[3] = optimizedClearColor_[3];

    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &colorClearValue,
                                                 IID_PPV_ARGS(&colorTex_));
    if (!HIKARI_DX_CHECK(hr, "RenderTarget2D::CreateCommittedResource color")) {
        DEBUGLOG::PushRenderError(DumpState());
        return false;
    }
    colorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    SetDebugName(debugName_);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    hr = device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_));
    if (!HIKARI_DX_CHECK(hr, "RenderTarget2D::CreateDescriptorHeap RTV")) {
        DEBUGLOG::PushRenderError(DumpState());
        return false;
    }
    rtvHandle_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(colorTex_.Get(), nullptr, rtvHandle_);

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    hr = device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap_));
    if (!HIKARI_DX_CHECK(hr, "RenderTarget2D::CreateDescriptorHeap SRV")) {
        DEBUGLOG::PushRenderError(DumpState());
        return false;
    }

    srvCpuHandle_ = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    srvGpuHandle_ = srvHeap_->GetGPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvView{};
    srvView.Format = format_;
    srvView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvView.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(colorTex_.Get(), &srvView, srvCpuHandle_);

    if (hasDepth_) {
        D3D12_CLEAR_VALUE depthClear{};
        depthClear.Format = DXGI_FORMAT_D32_FLOAT;
        depthClear.DepthStencil.Depth = 1.0f;
        depthClear.DepthStencil.Stencil = 0;

        CD3DX12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS, static_cast<UINT64>(width_), static_cast<UINT>(height_), 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                                             D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(&depthTex_));
        if (!HIKARI_DX_CHECK(hr, "RenderTarget2D::CreateCommittedResource depth")) {
            DEBUGLOG::PushRenderError(DumpState());
            return false;
        }
        depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
        dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvDesc.NumDescriptors = 2;
        dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_));
        if (!HIKARI_DX_CHECK(hr, "RenderTarget2D::CreateDescriptorHeap DSV")) {
            DEBUGLOG::PushRenderError(DumpState());
            return false;
        }
        dsvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvView{};
        dsvView.Format = DXGI_FORMAT_D32_FLOAT;
        dsvView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvView.Flags = D3D12_DSV_FLAG_NONE;
        device->CreateDepthStencilView(depthTex_.Get(), &dsvView, dsvHandle_);

        readOnlyDsvHandle_ = dsvHandle_;
        readOnlyDsvHandle_.ptr += dsvDescriptorSize_;

        D3D12_DEPTH_STENCIL_VIEW_DESC readOnlyDsvView = dsvView;
        readOnlyDsvView.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        device->CreateDepthStencilView(depthTex_.Get(), &readOnlyDsvView, readOnlyDsvHandle_);

        // Capture 専用 RT はメイン SceneDepth SRV を上書きしない。
    }
    SetDebugName(debugName_);

    return true;
}

void RenderTarget2D::TransitionDepth(D3D12_RESOURCE_STATES nextState) {
    if (!hasDepth_ || !depthTex_ || depthState_ == nextState) {
        return;
    }

    ID3D12GraphicsCommandList* cmd = context_.cmdList;
    if (!cmd) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(depthTex_.Get(), depthState_, nextState);
    cmd->ResourceBarrier(1, &barrier);
    depthState_ = nextState;
}

bool RenderTarget2D::CopyDepthToReadback(ID3D12Resource* destination,
                                         const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint) {
    if (!initialized_ || !depthTex_ || !destination || !context_.cmdList ||
        footprint.Footprint.Width != static_cast<UINT>(width_) ||
        footprint.Footprint.Height != static_cast<UINT>(height_) || footprint.Footprint.Depth != 1u ||
        footprint.Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0u ||
        footprint.Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT != 0u ||
        footprint.Footprint.RowPitch < static_cast<UINT>(width_) * sizeof(float))
        return false;
    const auto format = footprint.Footprint.Format;
    if (format != DXGI_FORMAT_R32_TYPELESS && format != DXGI_FORMAT_R32_FLOAT && format != DXGI_FORMAT_D32_FLOAT)
        return false;
    const auto destinationDesc = destination->GetDesc();
    const UINT64 requiredBytes =
        static_cast<UINT64>(height_ - 1) * footprint.Footprint.RowPitch + static_cast<UINT64>(width_) * sizeof(float);
    if (destinationDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || footprint.Offset > destinationDesc.Width ||
        requiredBytes > destinationDesc.Width - footprint.Offset)
        return false;
    const auto previous = depthState_;
    TransitionDepth(D3D12_RESOURCE_STATE_COPY_SOURCE);
    const CD3DX12_TEXTURE_COPY_LOCATION source(depthTex_.Get(), 0u);
    const CD3DX12_TEXTURE_COPY_LOCATION target(destination, footprint);
    context_.cmdList->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
    TransitionDepth(previous);
    return true;
}

void RenderTarget2D::TransitionColor(D3D12_RESOURCE_STATES nextState) {
    if (!initialized_ || !colorTex_ || colorState_ == nextState) {
        return;
    }

    ID3D12GraphicsCommandList* cmd = context_.cmdList;
    if (!cmd) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(colorTex_.Get(), colorState_, nextState);
    cmd->ResourceBarrier(1, &barrier);
    colorState_ = nextState;
}

void RenderTarget2D::BeginCapture(float r, float g, float b, float a, float depthClear) {
    if (!initialized_) {
        DEBUGLOG::PushRenderError(std::string("[RenderTarget2D][ERROR] BeginCapture skipped: not initialized. ") +
                                  DumpState());
        return;
    }

    ID3D12GraphicsCommandList* cmd = context_.cmdList;
    if (!cmd || !colorTex_ || !rtvHeap_) {
        DEBUGLOG::PushRenderError(
            std::string("[RenderTarget2D][ERROR] BeginCapture skipped: invalid command list/resource. ") + DumpState());
        return;
    }

    if (publishDepthSrv_ && hasDepth_ && !EnsureDepthSrv()) {
        DEBUGLOG::PushRenderError("[RenderTarget2D][ERROR] BeginCapture depth SRV publication failed.");
    }

    TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    if (hasDepth_) {
        TransitionDepth(D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, &dsvHandle_);
    } else {
        cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);
    }

    (void)ClearColor(r, g, b, a);

    if (hasDepth_) {
        cmd->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, depthClear, 0, 0, nullptr);
    }

    cmd->RSSetViewports(1, &viewport_);
    cmd->RSSetScissorRects(1, &scissorRect_);
}

bool RenderTarget2D::ClearColor(float r, float g, float b, float a) {
    ID3D12GraphicsCommandList* cmd = context_.cmdList;
    if (!initialized_ || cmd == nullptr || !colorTex_ || !rtvHeap_ || rtvHandle_.ptr == 0u) {
        return false;
    }
    TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    const float clearColor[4] = {r, g, b, a};
    cmd->ClearRenderTargetView(rtvHandle_, clearColor, 0, nullptr);
    return true;
}

void RenderTarget2D::Rebind() {
    if (!initialized_)
        return;

    ID3D12GraphicsCommandList* cmd = context_.cmdList;
    if (!cmd || !colorTex_ || !rtvHeap_) {
        DEBUGLOG::PushRenderError(
            std::string("[RenderTarget2D][ERROR] Rebind skipped: invalid command list/resource. ") + DumpState());
        return;
    }

    TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    if (hasDepth_) {
        TransitionDepth(D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, &dsvHandle_);
    } else {
        cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);
    }
    cmd->RSSetViewports(1, &viewport_);
    cmd->RSSetScissorRects(1, &scissorRect_);
}

bool RenderTarget2D::BeginDepthRead() {
    if (!initialized_ || !hasDepth_ || !depthTex_ || readOnlyDsvHandle_.ptr == 0) {
        return false;
    }

    ID3D12GraphicsCommandList* cmd = context_.cmdList;
    if (!cmd || !colorTex_ || !rtvHeap_) {
        DEBUGLOG::PushRenderError(
            std::string("[RenderTarget2D][ERROR] BeginDepthRead skipped: invalid command list/resource. ") +
            DumpState());
        return false;
    }

    if (publishDepthSrv_ && !EnsureDepthSrv()) {
        DEBUGLOG::PushRenderError("[RenderTarget2D][ERROR] BeginDepthRead depth SRV publication failed.");
        return false;
    }

    TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    TransitionDepth(D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, &readOnlyDsvHandle_);
    cmd->RSSetViewports(1, &viewport_);
    cmd->RSSetScissorRects(1, &scissorRect_);
    return true;
}

void RenderTarget2D::EndDepthRead() {
    if (!initialized_ || !hasDepth_ || !depthTex_) {
        return;
    }

    ID3D12GraphicsCommandList* cmd = context_.cmdList;
    if (!cmd || !colorTex_ || !rtvHeap_) {
        DEBUGLOG::PushRenderError(
            std::string("[RenderTarget2D][ERROR] EndDepthRead skipped: invalid command list/resource. ") + DumpState());
        return;
    }

    cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);
    TransitionDepth(D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, &dsvHandle_);
    cmd->RSSetViewports(1, &viewport_);
    cmd->RSSetScissorRects(1, &scissorRect_);
}

bool RenderTarget2D::BeginDepthShaderRead() {
    if (!initialized_ || !hasDepth_ || !depthTex_ || context_.cmdList == nullptr) {
        return false;
    }

    if (publishDepthSrv_ && !EnsureDepthSrv()) {
        DEBUGLOG::PushRenderError("[RenderTarget2D][ERROR] BeginDepthShaderRead depth SRV publication failed.");
        return false;
    }

    TransitionDepth(D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return true;
}

void RenderTarget2D::EndDepthShaderRead() {
    if (!initialized_ || !hasDepth_ || !depthTex_) {
        return;
    }

    TransitionDepth(D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void RenderTarget2D::EndCapture() {
    if (!initialized_) {
        DEBUGLOG::PushRenderError(std::string("[RenderTarget2D][ERROR] EndCapture skipped: not initialized. ") +
                                  DumpState());
        return;
    }

    ID3D12GraphicsCommandList* cmd = context_.cmdList;
    if (!cmd || !colorTex_) {
        DEBUGLOG::PushRenderError(
            std::string("[RenderTarget2D][ERROR] EndCapture skipped: invalid command list/resource. ") + DumpState());
        return;
    }

    TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

} // namespace HIKARI
