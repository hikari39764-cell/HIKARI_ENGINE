#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

#include <utility>

#include <d3dx12.h>

#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::SHADOW::INTERNAL {

bool CreateShadowMapResources(ShadowRendererState& state, const GFX::Context& graphics,
                              const ShadowMapAllocationDesc& allocation) {
    auto* device = graphics.device;
    const uint32_t resolution = allocation.tileResolution;
    if (device == nullptr || resolution == 0 || allocation.tilesPerRow == 0 ||
        allocation.tilesPerRow > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION / resolution) {
        return false;
    }
    InvalidateShadowCache(state);

    RENDER3D::ReleaseTextureResource(state.shadowSrvResource);
    state.shadowSrvResource = {};
    state.shadowSrvHandle = -1;

    // GPU がまだ参照している可能性があるため、Fence 経由の遅延解放に切り替える
    GFX::RetireD3D12ObjectForFrame(state.shadowMap, graphics, "ShadowRenderer.ShadowMap");
    GFX::RetireD3D12ObjectForFrame(state.staticShadowMap, graphics, "ShadowRenderer.StaticShadowMap");
    GFX::RetireD3D12ObjectForFrame(state.dsvHeap, graphics, "ShadowRenderer.DsvHeap");
    state.staticShadowState = D3D12_RESOURCE_STATE_COMMON;
    state.shadowCache.SetFinalMatchesStaticCache(false);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(state.dsvHeap.GetAddressOf())))) {
        return false;
    }

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const uint32_t atlasResolution = resolution * allocation.tilesPerRow;
    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, atlasResolution, atlasResolution, 1, 1, 1, 0,
                                                D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;
    const HRESULT hr = GFX::VIDEO_MEMORY::CreateCommittedResource(
        device, &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, kShadowShaderReadState, &clearValue,
        GFX::VIDEO_MEMORY::Owner::Shadow, IID_PPV_ARGS(state.shadowMap.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "ShadowMapRenderer::CreateShadowMapResource")) {
        return false;
    }
    GFX::SetD3D12Name(state.shadowMap.Get(), L"Shadow Depth Atlas");

    if (allocation.cacheStaticDepth) {
        const HRESULT staticHr = GFX::VIDEO_MEMORY::CreateCommittedResource(
            device, &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COMMON, &clearValue,
            GFX::VIDEO_MEMORY::Owner::Shadow, IID_PPV_ARGS(state.staticShadowMap.GetAddressOf()));
        if (!HIKARI_DX_CHECK(staticHr, "ShadowMapRenderer::CreateStaticShadowCacheResource")) {
            // 部分初期化失敗時も GPU 参照を待ってから解放する
            GFX::RetireD3D12ObjectForFrame(state.shadowMap, graphics, "ShadowRenderer.ShadowMap.PartialInit");
            return false;
        }
        GFX::SetD3D12Name(state.staticShadowMap.Get(), L"Shadow Static Depth Cache");
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    state.dsv = state.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateDepthStencilView(state.shadowMap.Get(), &dsvDesc, state.dsv);

    RENDER3D::RenderResourceDesc shadowSrvDesc{};
    shadowSrvDesc.kind = RENDER3D::RenderResourceKind::Texture;
    shadowSrvDesc.usage =
        RENDER3D::RenderResourceUsageFlags::ShaderResource | RENDER3D::RenderResourceUsageFlags::DepthStencil;
    shadowSrvDesc.lifetime = RENDER3D::RenderResourceLifetime::External;
    shadowSrvDesc.debugName = "Shadow.DepthAtlas";
    shadowSrvDesc.sourceKey = allocation.sourceKey != nullptr ? allocation.sourceKey : "";
    shadowSrvDesc.width = atlasResolution;
    shadowSrvDesc.height = atlasResolution;
    shadowSrvDesc.mipLevels = 1;
    shadowSrvDesc.depthOrArraySize = 1;
    shadowSrvDesc.format = DXGI_FORMAT_R32_FLOAT;
    state.shadowSrvResource = RENDER3D::RegisterTextureResourceFromNative(state.shadowMap.Get(), DXGI_FORMAT_R32_FLOAT,
                                                                          std::move(shadowSrvDesc));
    state.shadowSrvHandle = RENDER3D::GetTextureResourceBackendHandle(state.shadowSrvResource);
    state.resolution = resolution;
    state.atlasResolution = atlasResolution;
    state.atlasTilesPerRow = allocation.tilesPerRow;
    state.shadowState = kShadowShaderReadState;
    state.staticShadowState = D3D12_RESOURCE_STATE_COMMON;
    ++state.shadowMapRecreateCount;
    return RENDER3D::IsTextureResourceValid(state.shadowSrvResource);
}
void RestoreMainRenderTarget(const GFX::Context& graphics) {
    if (POST::PostSystem::RebindCurrentRenderTarget()) {
        return;
    }

    auto* cmd = graphics.cmdList;
    if (cmd == nullptr) {
        return;
    }
    cmd->OMSetRenderTargets(1, &graphics.rtv, FALSE, &graphics.dsv);
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(graphics.backBufferWidth);
    viewport.Height = static_cast<float>(graphics.backBufferHeight);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, graphics.backBufferWidth, graphics.backBufferHeight};
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
}

void TransitionResource(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES& currentState,
                        D3D12_RESOURCE_STATES targetState) {
    if (cmd == nullptr || resource == nullptr || currentState == targetState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, currentState, targetState);
    cmd->ResourceBarrier(1, &barrier);
    currentState = targetState;
}

void PrepareFinalShadowMapForDepthWrite(ShadowRendererState& state, ID3D12GraphicsCommandList* cmd, bool clearDepth) {
    const uint32_t tile = state.activeCascadeIndex;
    const LONG left = LONG((tile % state.atlasTilesPerRow) * state.resolution);
    const LONG top = LONG((tile / state.atlasTilesPerRow) * state.resolution);
    const D3D12_RECT region{left, top, left + LONG(state.resolution), top + LONG(state.resolution)};
    (void)PrepareShadowMapDepthRegion(state, cmd, region, clearDepth);
}

bool PrepareShadowMapDepthRegion(ShadowRendererState& state, ID3D12GraphicsCommandList* cmd, const D3D12_RECT& region,
                                 bool clearDepth) {
    if (cmd == nullptr || !state.shadowMap || region.left < 0 || region.top < 0 || region.right <= region.left ||
        region.bottom <= region.top || region.right > LONG(state.atlasResolution) ||
        region.bottom > LONG(state.atlasResolution))
        return false;
    TransitionResource(cmd, state.shadowMap.Get(), state.shadowState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    const D3D12_VIEWPORT viewport{float(region.left),
                                  float(region.top),
                                  float(region.right - region.left),
                                  float(region.bottom - region.top),
                                  0.f,
                                  1.f};
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &region);
    cmd->OMSetRenderTargets(0, nullptr, FALSE, &state.dsv);
    if (clearDepth)
        cmd->ClearDepthStencilView(state.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 1, &region);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
    if (srvHeap != nullptr)
        cmd->SetDescriptorHeaps(1, &srvHeap);
    return true;
}

void FinishFinalShadowMap(ShadowRendererState& state, const GFX::Context& graphics, ID3D12GraphicsCommandList* cmd) {
    TransitionResource(cmd, state.shadowMap.Get(), state.shadowState, kShadowShaderReadState);
    RestoreMainRenderTarget(graphics);
}

bool CopyStaticShadowCacheToFinal(ShadowRendererState& state, ID3D12GraphicsCommandList* cmd) {
    if (cmd == nullptr || state.staticShadowMap == nullptr || state.shadowMap == nullptr ||
        !state.shadowCache.IsValid()) {
        return false;
    }

    if (!state.frameHasDynamicShadowWork && state.shadowCache.FinalMatchesStaticCache() &&
        state.shadowState == kShadowShaderReadState) {
        return true;
    }

    TransitionResource(cmd, state.staticShadowMap.Get(), state.staticShadowState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    TransitionResource(cmd, state.shadowMap.Get(), state.shadowState, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(state.shadowMap.Get(), state.staticShadowMap.Get());
    state.shadowCache.RecordCopy(!state.frameHasDynamicShadowWork);
    PublishShadowCacheStats(state);
    return true;
}

bool UpdateStaticShadowCacheFromFinal(ShadowRendererState& state, ID3D12GraphicsCommandList* cmd) {
    if (cmd == nullptr || state.staticShadowMap == nullptr || state.shadowMap == nullptr ||
        !state.frameHasStaticShadowWork) {
        return false;
    }

    TransitionResource(cmd, state.shadowMap.Get(), state.shadowState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    TransitionResource(cmd, state.staticShadowMap.Get(), state.staticShadowState, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(state.staticShadowMap.Get(), state.shadowMap.Get());
    TransitionResource(cmd, state.staticShadowMap.Get(), state.staticShadowState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    MarkShadowCacheValidAfterRender(state);
    return true;
}

} // namespace HIKARI::SHADOW::INTERNAL
