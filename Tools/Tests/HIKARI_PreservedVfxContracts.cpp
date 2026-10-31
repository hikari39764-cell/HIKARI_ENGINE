#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <d3dx12.h>
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/HIKARI_TimeService.h"
#include "Vfx/Post/HIKARI_PreservedVfxLayer.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Internal/HIKARI_GpuContractDevice.h"

void CheckPreservedVfxLayer() {
    using namespace HIKARI;
    using namespace RENDER3D;
    using Microsoft::WRL::ComPtr;
    assert(CONTENT::GetEngineContentContext().Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT));
    TESTS::INTERNAL::GpuContractDevice gpu;
    const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1,
                                              D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ComPtr<ID3D12DescriptorHeap> heap;
    assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap))));
    GFX::Context graphics{};
    graphics.device = gpu.device.Get();
    graphics.deviceGeneration = 1;
    graphics.cmdList = gpu.commands.Get();
    graphics.queue = gpu.queue.Get();
    graphics.srvHeap = heap.Get();
    POST::QuadDrawer quad;
    assert(quad.Init(graphics));
    POST::PreservedVfxLayer layer;
    TIME::Reset();
    for (int iteration = 0; iteration != 3; ++iteration) {
        const auto frameIndex = TIME::BeginFrame().frameIndex;
        const int renderWidth = iteration == 2 ? 8 : 16;
        const int outputWidth = renderWidth * 2;
        gpu.ResetCommands();
        RenderTarget2D scene, alpha, additive, final;
        for (auto* target : {&scene, &alpha, &additive, &final})
            target->UpdateContext(graphics);
        assert(scene.Init(renderWidth, 4, DXGI_FORMAT_R16G16B16A16_FLOAT, true));
        assert(alpha.Init(1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT));
        assert(additive.Init(1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT));
        assert(final.Init(outputWidth, 8, DXGI_FORMAT_R32G32B32A32_FLOAT));
        alpha.BeginCapture(2, .5f, 0, .5f);
        alpha.EndCapture();
        additive.BeginCapture(.5f, 1, 2, 0);
        additive.EndCapture();
        scene.BeginCapture(.2f, .4f, .6f, 1);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        gpu.device->CreateShaderResourceView(scene.GetDepthResource(), &srv,
                                             heap->GetCPUDescriptorHandleForHeapStart());
        PIPELINE::RenderFrameContext frame{};
        frame.token = AllocateRenderFrameToken(frameIndex, kPrimaryRenderViewId);
        frame.graphics = graphics;
        frame.renderWidth = static_cast<uint32_t>(renderWidth);
        frame.renderHeight = 4;
        frame.scene.readOnlyDsv = scene.GetReadOnlyDsvHandle();
        frame.scene.depth = {scene.GetDepthResource(),
                             heap->GetGPUDescriptorHandleForHeapStart(),
                             DXGI_FORMAT_R32_FLOAT,
                             frame.renderWidth,
                             4,
                             1,
                             D3D12_RESOURCE_STATE_DEPTH_WRITE,
                             true};
        frame.targetAccess.userData = &scene;
        frame.targetAccess.rebind = [](void* target) {
            static_cast<RenderTarget2D*>(target)->Rebind();
            return true;
        };
        frame.targetAccess.beginDepthRead = [](void* target) {
            return static_cast<RenderTarget2D*>(target)->BeginDepthRead();
        };
        assert(SURFACE::BeginRenderFrameSurfaces(frame.token, 1, frame.surfaceReceipt));
        TEMPORAL::TemporalFrameState temporal{};
        temporal.view = {frame.token.viewId, 1};
        temporal.publication = frame.token;
        temporal.camera.valid = true;
        temporal.camera.jitter = {.5f, 0, 0, 0};
        temporal.renderWidth = frame.renderWidth;
        temporal.renderHeight = frame.renderHeight;
        PIPELINE::RenderTargetAccess target{};
        auto invalid = frame;
        invalid.renderWidth++;
        assert(!layer.Begin(invalid, temporal, target));
        assert(layer.Begin(frame, temporal, target));
        assert(!layer.Begin(frame, temporal, target));
        assert(quad.SetOutputFormat(DXGI_FORMAT_R16G16B16A16_FLOAT));
        const D3D12_RECT leftHalf{0, 0, renderWidth / 2, 4};
        graphics.cmdList->RSSetScissorRects(1, &leftHalf);
        quad.DrawBlended(alpha.GetSrvHeap(), alpha.GetSrvGpu(), POST::BlendOption::Alpha, 0);
        quad.DrawBlended(additive.GetSrvHeap(), additive.GetSrvGpu(), POST::BlendOption::Additive, 0);
        assert(layer.End(true));
        RenderTarget2D* preserved = nullptr;
        auto stale = frame.surfaceReceipt;
        stale.registryGeneration++;
        assert(!layer.Resolve(stale, {}, outputWidth, 8, quad, preserved));
        const MATH::Vec2 outputJitter = iteration == 0 ? MATH::Vec2{.5f, 0} : MATH::Vec2{};
        assert(layer.Resolve(frame.surfaceReceipt, outputJitter, outputWidth, 8, quad, preserved) && preserved);
        final.BeginCapture(.2f, .4f, .6f, 1);
        assert(quad.SetOutputFormat(final.GetFormat()));
        quad.DrawBlended(preserved->GetSrvHeap(), preserved->GetSrvGpu(), POST::BlendOption::PremultipliedAlpha, 0);
        final.EndCapture();
        assert(layer.Resolve(frame.surfaceReceipt, outputJitter, outputWidth, 8, quad, preserved) && !preserved);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 bytes = 0;
        const auto desc = final.GetResource()->GetDesc();
        gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(bytes);
        const CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
        ComPtr<ID3D12Resource> readback;
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                             IID_PPV_ARGS(&readback))));
        final.TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
        const CD3DX12_TEXTURE_COPY_LOCATION from(final.GetResource(), 0);
        const CD3DX12_TEXTURE_COPY_LOCATION to(readback.Get(), footprint);
        graphics.cmdList->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        gpu.SubmitAndWait(static_cast<uint64_t>(iteration + 1));
        void* mapped = nullptr;
        assert(SUCCEEDED(readback->Map(0, nullptr, &mapped)));
        const auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped) + footprint.Offset +
                                                         footprint.Footprint.RowPitch * 3);
        // Alpha fill + additive RGB retain .5 coverage; outside pixels preserve scene HDR.
        for (int x = 1; x < outputWidth - 1; ++x) {
            const float sourceX = (static_cast<float>(x) + .5f) * .5f + (iteration ? .5f : 0.f);
            const float weight = std::clamp(static_cast<float>(renderWidth / 2) + .5f - sourceX, 0.f, 1.f);
            const std::array<float, 4> expected{.2f + 1.4f * weight, .4f + 1.05f * weight, .6f + 1.7f * weight, 1};
            for (int c = 0; c != 4; ++c)
                assert(std::abs(row[x * 4 + c] - expected[c]) < .004f);
        }
        readback->Unmap(0, nullptr);
        layer.Invalidate();
        assert(SURFACE::EndRenderFrameSurfaces(frame.surfaceReceipt));
    }
    layer.Shutdown();
    quad.Finalize();
    std::puts("Preserved VFX GPU: PMA/additive, coverage, scale, output jitter, resize, stale receipt PASS");
}
