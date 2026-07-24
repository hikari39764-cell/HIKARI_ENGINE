#include "Render2D/HIKARI_RenderTarget2D.h"
#include <cassert>
#include <sstream>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI {

    bool RenderTarget2D::Init(
        int width,
        int height,
        DXGI_FORMAT format,
        bool withDepth,
        const std::array<float, 4>& optimizedClearColor,
        bool publishDepthSrv,
        bool allowUnorderedAccess)
    {
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

        if (!CreateResources()) {
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

    void RenderTarget2D::UpdateContext(const HIKARI::GFX::Context& ctx)
    {
        context_ = ctx;
    }

    void RenderTarget2D::Finalize()
    {
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
        depthSrvGpuHandle_ = {};
        dsvDescriptorSize_ = 0;

        initialized_ = false;
        hasDepth_ = false;
        publishDepthSrv_ = true;
        allowUnorderedAccess_ = false;
    }

    void RenderTarget2D::SetDebugName(std::string name)
    {
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

    std::string RenderTarget2D::DumpState() const
    {
        std::ostringstream oss;
        oss << "[RenderTarget2D] name=" << debugName_
            << " initialized=" << initialized_
            << " size=" << width_ << "x" << height_
            << " format=" << GFX::FormatToString(format_)
            << " hasDepth=" << hasDepth_
            << " allowUav=" << allowUnorderedAccess_
            << " colorTex=" << (colorTex_ ? 1 : 0)
            << " depthTex=" << (depthTex_ ? 1 : 0)
            << " rtvHeap=" << (rtvHeap_ ? 1 : 0)
            << " srvHeap=" << (srvHeap_ ? 1 : 0)
            << " dsvHeap=" << (dsvHeap_ ? 1 : 0)
            << " srvGpu=0x" << std::hex << srvGpuHandle_.ptr << std::dec
            << " depthSrvGpu=0x" << std::hex << depthSrvGpuHandle_.ptr << std::dec
            << " colorState=" << GFX::ResourceStateToString(colorState_)
            << " depthState=" << GFX::ResourceStateToString(depthState_);
        return oss.str();
    }

bool RenderTarget2D::CreateResources()
{
    ID3D12Device* device = context_.device;
    if (!device) { return false; }

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_FLAGS colorFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (allowUnorderedAccess_) {
        colorFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        format_,
        static_cast<UINT64>(width_),
        static_cast<UINT>(height_),
        1, 1, 1, 0,
        colorFlags
    );

    D3D12_CLEAR_VALUE colorClearValue{};
    colorClearValue.Format = format_;
    colorClearValue.Color[0] = optimizedClearColor_[0];
    colorClearValue.Color[1] = optimizedClearColor_[1];
    colorClearValue.Color[2] = optimizedClearColor_[2];
    colorClearValue.Color[3] = optimizedClearColor_[3];

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &colorClearValue,
        IID_PPV_ARGS(&colorTex_)
    );
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
            DXGI_FORMAT_R32_TYPELESS,
            static_cast<UINT64>(width_),
            static_cast<UINT>(height_),
            1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        );

        hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthClear,
            IID_PPV_ARGS(&depthTex_)
        );
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

        // Capture 蟆ら畑 RT 縺ｯ繝｡繧､繝ｳ SceneDepth SRV 繧剃ｸ頑嶌縺阪＠縺ｪ縺・・
        if (publishDepthSrv_ && context_.srvHeap != nullptr) {
            const UINT srvDescriptorSize =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const UINT postSceneDepthIndex =
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::PostSceneDepth);
            D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu =
                GFX::DESCRIPTOR::CpuAt(context_.srvHeap, srvDescriptorSize, postSceneDepthIndex);
            D3D12_GPU_DESCRIPTOR_HANDLE depthSrvGpu =
                GFX::DESCRIPTOR::GpuAt(context_.srvHeap, srvDescriptorSize, postSceneDepthIndex);
            D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvView{};
            depthSrvView.Format = DXGI_FORMAT_R32_FLOAT;
            depthSrvView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            depthSrvView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            depthSrvView.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(depthTex_.Get(), &depthSrvView, depthSrvCpu);
            depthSrvGpuHandle_ = depthSrvGpu;
        }
    }
    SetDebugName(debugName_);

    return true;
}

    void RenderTarget2D::TransitionDepth(D3D12_RESOURCE_STATES nextState)
    {
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

    void RenderTarget2D::TransitionColor(D3D12_RESOURCE_STATES nextState)
    {
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
    
    void RenderTarget2D::BeginCapture(float r, float g, float b, float a, float depthClear)
    {
        if (!initialized_) {
            DEBUGLOG::PushRenderError(std::string("[RenderTarget2D][ERROR] BeginCapture skipped: not initialized. ") + DumpState());
            return;
        }

        ID3D12GraphicsCommandList* cmd = context_.cmdList;
        if (!cmd || !colorTex_ || !rtvHeap_) {
            DEBUGLOG::PushRenderError(std::string("[RenderTarget2D][ERROR] BeginCapture skipped: invalid command list/resource. ") + DumpState());
            return;
        }

        TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

        if (hasDepth_) {
            TransitionDepth(D3D12_RESOURCE_STATE_DEPTH_WRITE);
            cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, &dsvHandle_);
        } else {
            cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);
        }

        float clearColor[4] = { r, g, b, a };
        cmd->ClearRenderTargetView(rtvHandle_, clearColor, 0, nullptr);

        if (hasDepth_) {
            cmd->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, depthClear, 0, 0, nullptr);
        }

        cmd->RSSetViewports(1, &viewport_);
        cmd->RSSetScissorRects(1, &scissorRect_);
    }

    void RenderTarget2D::Rebind()
    {
        if (!initialized_) return;

        ID3D12GraphicsCommandList* cmd = context_.cmdList;
        if (!cmd || !colorTex_ || !rtvHeap_) {
            DEBUGLOG::PushRenderError(std::string("[RenderTarget2D][ERROR] Rebind skipped: invalid command list/resource. ") + DumpState());
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

    bool RenderTarget2D::BeginDepthRead()
    {
        if (!initialized_ || !hasDepth_ || !depthTex_ || readOnlyDsvHandle_.ptr == 0) {
            return false;
        }

        ID3D12GraphicsCommandList* cmd = context_.cmdList;
        if (!cmd || !colorTex_ || !rtvHeap_) {
            DEBUGLOG::PushRenderError(std::string("[RenderTarget2D][ERROR] BeginDepthRead skipped: invalid command list/resource. ") + DumpState());
            return false;
        }

        TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

        TransitionDepth(
            D3D12_RESOURCE_STATE_DEPTH_READ |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, &readOnlyDsvHandle_);
        cmd->RSSetViewports(1, &viewport_);
        cmd->RSSetScissorRects(1, &scissorRect_);
        return true;
    }

    void RenderTarget2D::EndDepthRead()
    {
        if (!initialized_ || !hasDepth_ || !depthTex_) {
            return;
        }

        ID3D12GraphicsCommandList* cmd = context_.cmdList;
        if (!cmd || !colorTex_ || !rtvHeap_) {
            DEBUGLOG::PushRenderError(std::string("[RenderTarget2D][ERROR] EndDepthRead skipped: invalid command list/resource. ") + DumpState());
            return;
        }

        cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);
        TransitionDepth(D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, &dsvHandle_);
        cmd->RSSetViewports(1, &viewport_);
        cmd->RSSetScissorRects(1, &scissorRect_);
    }

    bool RenderTarget2D::BeginDepthShaderRead()
    {
        if (!initialized_ || !hasDepth_ || !depthTex_ || depthSrvGpuHandle_.ptr == 0) {
            return false;
        }

        TransitionDepth(
            D3D12_RESOURCE_STATE_DEPTH_READ |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return true;
    }

    void RenderTarget2D::EndDepthShaderRead()
    {
        if (!initialized_ || !hasDepth_ || !depthTex_) {
            return;
        }

        TransitionDepth(D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }

    void RenderTarget2D::EndCapture()
    {
        if (!initialized_) {
            DEBUGLOG::PushRenderError(std::string("[RenderTarget2D][ERROR] EndCapture skipped: not initialized. ") + DumpState());
            return;
        }

        ID3D12GraphicsCommandList* cmd = context_.cmdList;
        if (!cmd || !colorTex_) {
            DEBUGLOG::PushRenderError(std::string("[RenderTarget2D][ERROR] EndCapture skipped: invalid command list/resource. ") + DumpState());
            return;
        }

        TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

} // namespace HIKARI
