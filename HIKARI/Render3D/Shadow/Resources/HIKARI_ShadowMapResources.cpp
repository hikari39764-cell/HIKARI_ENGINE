#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <utility>

#include <d3dx12.h>

#include "HIKARI_Services.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::SHADOW::INTERNAL {

    bool CreateShadowMapResources(uint32_t resolution) {
        auto* device = SERVICES::gCtx.device;
        if (device == nullptr || resolution == 0) {
            return false;
        }
        InvalidateShadowCache();

        RENDER3D::ReleaseTextureResource(gShadowRendererState.shadowSrvResource);
        gShadowRendererState.shadowSrvResource = {};
        gShadowRendererState.shadowSrvHandle = -1;

        gShadowRendererState.shadowMap.Reset();
        gShadowRendererState.staticShadowMap.Reset();
        gShadowRendererState.dsvHeap.Reset();
        gShadowRendererState.staticShadowState = D3D12_RESOURCE_STATE_COMMON;
        gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(false);

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(gShadowRendererState.dsvHeap.GetAddressOf())))) {
            return false;
        }

        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS,
            resolution,
            resolution,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DXGI_FORMAT_D32_FLOAT;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            kShadowShaderReadState,
            &clearValue,
            IID_PPV_ARGS(gShadowRendererState.shadowMap.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "ShadowMapRenderer::CreateShadowMapResource")) {
            return false;
        }
        GFX::SetD3D12Name(gShadowRendererState.shadowMap.Get(), L"Directional Shadow Map");

        const HRESULT staticHr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COMMON,
            &clearValue,
            IID_PPV_ARGS(gShadowRendererState.staticShadowMap.GetAddressOf()));
        if (!HIKARI_DX_CHECK(staticHr, "ShadowMapRenderer::CreateStaticShadowCacheResource")) {
            gShadowRendererState.shadowMap.Reset();
            return false;
        }
        GFX::SetD3D12Name(gShadowRendererState.staticShadowMap.Get(), L"Directional Static Shadow Cache");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        gShadowRendererState.dsv = gShadowRendererState.dsvHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateDepthStencilView(gShadowRendererState.shadowMap.Get(), &dsvDesc, gShadowRendererState.dsv);

        RENDER3D::RenderResourceDesc shadowSrvDesc{};
        shadowSrvDesc.kind = RENDER3D::RenderResourceKind::Texture;
        shadowSrvDesc.usage =
            RENDER3D::RenderResourceUsageFlags::ShaderResource |
            RENDER3D::RenderResourceUsageFlags::DepthStencil;
        shadowSrvDesc.lifetime = RENDER3D::RenderResourceLifetime::External;
        shadowSrvDesc.debugName = "directional_shadow_map_srv";
        shadowSrvDesc.sourceKey = "shadow/directional_map";
        shadowSrvDesc.width = resolution;
        shadowSrvDesc.height = resolution;
        shadowSrvDesc.mipLevels = 1;
        shadowSrvDesc.depthOrArraySize = 1;
        shadowSrvDesc.format = DXGI_FORMAT_R32_FLOAT;
        gShadowRendererState.shadowSrvResource = RENDER3D::RegisterTextureResourceFromNative(
            gShadowRendererState.shadowMap.Get(),
            DXGI_FORMAT_R32_FLOAT,
            std::move(shadowSrvDesc));
        gShadowRendererState.shadowSrvHandle = RENDER3D::GetTextureResourceBackendHandle(gShadowRendererState.shadowSrvResource);
        gShadowRendererState.resolution = resolution;
        gShadowRendererState.shadowState = kShadowShaderReadState;
        gShadowRendererState.staticShadowState = D3D12_RESOURCE_STATE_COMMON;
        ++gShadowRendererState.shadowMapRecreateCount;
        return RENDER3D::IsTextureResourceValid(gShadowRendererState.shadowSrvResource);
    }
    void RestoreMainRenderTarget() {
        if (POST::PostSystem::RebindCurrentRenderTarget()) {
            return;
        }

        auto* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr) {
            return;
        }
        cmd->OMSetRenderTargets(1, &SERVICES::gCtx.rtv, FALSE, &SERVICES::gCtx.dsv);
        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(SERVICES::gCtx.backBufferWidth);
        viewport.Height = static_cast<float>(SERVICES::gCtx.backBufferHeight);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{ 0, 0, SERVICES::gCtx.backBufferWidth, SERVICES::gCtx.backBufferHeight };
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);
    }

    void TransitionResource(
        ID3D12GraphicsCommandList* cmd,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES& currentState,
        D3D12_RESOURCE_STATES targetState) {

        if (cmd == nullptr || resource == nullptr || currentState == targetState) {
            return;
        }

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            resource,
            currentState,
            targetState);
        cmd->ResourceBarrier(1, &barrier);
        currentState = targetState;
    }

    void PrepareFinalShadowMapForDepthWrite(ID3D12GraphicsCommandList* cmd, bool clearDepth) {
        if (cmd == nullptr || gShadowRendererState.shadowMap == nullptr) {
            return;
        }

        TransitionResource(
            cmd,
            gShadowRendererState.shadowMap.Get(),
            gShadowRendererState.shadowState,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(gShadowRendererState.resolution);
        viewport.Height = static_cast<float>(gShadowRendererState.resolution);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{
            0,
            0,
            static_cast<LONG>(gShadowRendererState.resolution),
            static_cast<LONG>(gShadowRendererState.resolution)
        };
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);
        cmd->OMSetRenderTargets(0, nullptr, FALSE, &gShadowRendererState.dsv);
        if (clearDepth) {
            cmd->ClearDepthStencilView(
                gShadowRendererState.dsv,
                D3D12_CLEAR_FLAG_DEPTH,
                1.0f,
                0,
                0,
                nullptr);
        }
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }
    }

    void FinishFinalShadowMap(ID3D12GraphicsCommandList* cmd) {
        TransitionResource(
            cmd,
            gShadowRendererState.shadowMap.Get(),
            gShadowRendererState.shadowState,
            kShadowShaderReadState);
        RestoreMainRenderTarget();
    }

    bool CopyStaticShadowCacheToFinal(ID3D12GraphicsCommandList* cmd) {
        if (cmd == nullptr ||
            gShadowRendererState.staticShadowMap == nullptr ||
            gShadowRendererState.shadowMap == nullptr ||
            !gShadowRendererState.shadowCache.IsValid()) {
            return false;
        }

        if (!gShadowRendererState.frameHasDynamicShadowWork &&
            gShadowRendererState.shadowCache.FinalMatchesStaticCache() &&
            gShadowRendererState.shadowState == kShadowShaderReadState) {
            return true;
        }

        TransitionResource(
            cmd,
            gShadowRendererState.staticShadowMap.Get(),
            gShadowRendererState.staticShadowState,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        TransitionResource(
            cmd,
            gShadowRendererState.shadowMap.Get(),
            gShadowRendererState.shadowState,
            D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(gShadowRendererState.shadowMap.Get(), gShadowRendererState.staticShadowMap.Get());
        gShadowRendererState.shadowCache.RecordCopy(!gShadowRendererState.frameHasDynamicShadowWork);
        PublishShadowCacheStats();
        return true;
    }

    bool UpdateStaticShadowCacheFromFinal(ID3D12GraphicsCommandList* cmd) {
        if (cmd == nullptr ||
            gShadowRendererState.staticShadowMap == nullptr ||
            gShadowRendererState.shadowMap == nullptr ||
            !gShadowRendererState.frameHasStaticShadowWork) {
            return false;
        }

        TransitionResource(
            cmd,
            gShadowRendererState.shadowMap.Get(),
            gShadowRendererState.shadowState,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        TransitionResource(
            cmd,
            gShadowRendererState.staticShadowMap.Get(),
            gShadowRendererState.staticShadowState,
            D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(gShadowRendererState.staticShadowMap.Get(), gShadowRendererState.shadowMap.Get());
        TransitionResource(
            cmd,
            gShadowRendererState.staticShadowMap.Get(),
            gShadowRendererState.staticShadowState,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        MarkShadowCacheValidAfterRender();
        return true;
    }

} // namespace HIKARI::SHADOW::INTERNAL
