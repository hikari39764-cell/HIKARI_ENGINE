#include "Render3D/Pipeline/Internal/HIKARI_ProjectDispatchOutput.h"

#include <array>
#include <thread>
#include <utility>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ResourceStateTracker.h"
#include "Render3D/Graph/HIKARI_RenderGraphSurfaceContract.h"
#include "Render3D/Pipeline/HIKARI_ProjectComputeShaderRuntime.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_RenderResourceSystem.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::PIPELINE {
namespace {

    using Microsoft::WRL::ComPtr;
    constexpr uint32_t kDescriptorsPerFrame = 3u;
    constexpr DXGI_FORMAT kDispatchOutputFormat =
        DXGI_FORMAT_R16G16B16A16_FLOAT;
    constexpr D3D12_RESOURCE_STATES kDispatchReadState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_COPY_SOURCE;

    struct OutputFrame {
        ComPtr<ID3D12Resource> resource{};
        RenderResourceHandle poolHandle{};
        RenderResourceView inputSrv{};
        RenderResourceView outputUav{};
        RenderResourceView outputSrv{};
        RenderFrameToken publishedFrame{};
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    struct OutputState {
        std::thread::id ownerThread{};
        GFX::Context graphics{};
        std::array<OutputFrame, GFX::kFrameResourceCount> outputs{};
        std::vector<RenderResourceView> descriptors{};
    };

    OutputState& State() noexcept {
        static OutputState state{};
        return state;
    }

    void RetireOutput(OutputState& state, OutputFrame& output) noexcept {
        if (output.resource != nullptr &&
            state.graphics.resourceStates != nullptr) {
            state.graphics.resourceStates->Forget(output.resource.Get());
        }
        if (output.poolHandle.IsValid()) {
            (void)GetRenderResourcePool().Release(output.poolHandle);
        }
        GFX::RetireD3D12ObjectForFrame(
            output.resource,
            state.graphics,
            "ProjectDispatch.Output");
        const RenderResourceView inputSrv = output.inputSrv;
        const RenderResourceView outputUav = output.outputUav;
        const RenderResourceView outputSrv = output.outputSrv;
        output = {};
        output.inputSrv = inputSrv;
        output.outputUav = outputUav;
        output.outputSrv = outputSrv;
    }

    void RetireState(OutputState& state, bool releaseDescriptors) noexcept {
        for (OutputFrame& output : state.outputs) {
            RetireOutput(state, output);
        }
        if (releaseDescriptors) {
            for (const RenderResourceView view : state.descriptors) {
                (void)ReleaseRenderResourceDescriptor(view);
            }
            for (OutputFrame& output : state.outputs) {
                output.inputSrv = {};
                output.outputUav = {};
                output.outputSrv = {};
            }
        }
        state.descriptors.clear();
        state.graphics = {};
        state.ownerThread = {};
    }

    bool PrepareState(
        OutputState& state,
        const GFX::Context& graphics,
        std::string& outDiagnostic) {
        if (graphics.device == nullptr || graphics.cmdList == nullptr ||
            graphics.srvHeap == nullptr || graphics.resourceStates == nullptr ||
            graphics.deviceGeneration == 0u ||
            graphics.frameIndex >= GFX::kFrameResourceCount) {
            outDiagnostic = "project Dispatch output context is unavailable";
            return false;
        }
        const std::thread::id caller = std::this_thread::get_id();
        if (state.ownerThread != std::thread::id{} &&
            state.ownerThread != caller) {
            outDiagnostic = "project Dispatch output used off owner thread";
            return false;
        }
        if (state.graphics.deviceGeneration != 0u &&
            (state.graphics.deviceGeneration != graphics.deviceGeneration ||
             state.graphics.device != graphics.device ||
             state.graphics.srvHeap != graphics.srvHeap)) {
            // The descriptor range belongs to the old heap. Release it while
            // the descriptor-pool owner context still names that heap, then
            // let EnsureDescriptorRange allocate a contiguous range for the
            // new context. Keeping the numeric slots across generations would
            // make the table point at stale CPU/GPU descriptor handles.
            RetireState(state, true);
        }
        if (state.ownerThread == std::thread::id{}) {
            state.ownerThread = caller;
        }
        state.graphics = graphics;
        return true;
    }

    bool EnsureDescriptorRange(
        OutputState& state,
        std::string& outDiagnostic) {
        if (!state.descriptors.empty()) {
            return state.descriptors.size() ==
                GFX::kFrameResourceCount * kDescriptorsPerFrame;
        }
        state.descriptors = AllocateRenderResourceDescriptorRange(
            GFX::kFrameResourceCount * kDescriptorsPerFrame);
        if (state.descriptors.size() !=
                GFX::kFrameResourceCount * kDescriptorsPerFrame) {
            state.descriptors.clear();
            outDiagnostic = "project Dispatch descriptor range is unavailable";
            return false;
        }
        for (uint32_t frameSlot = 0u;
            frameSlot < GFX::kFrameResourceCount;
            ++frameSlot) {
            OutputFrame& output = state.outputs[frameSlot];
            const size_t base = frameSlot * kDescriptorsPerFrame;
            output.inputSrv = state.descriptors[base + 0u];
            output.outputUav = state.descriptors[base + 1u];
            output.outputSrv = state.descriptors[base + 2u];
        }
        return true;
    }

    bool EnsureOutputResource(
        OutputState& state,
        OutputFrame& output,
        uint32_t width,
        uint32_t height,
        std::string& outDiagnostic) {
        if (output.resource != nullptr && output.width == width &&
            output.height == height) {
            return true;
        }
        RetireOutput(state, output);
        if (!output.inputSrv.IsValid() || !output.outputUav.IsValid() ||
            !output.outputSrv.IsValid()) {
            outDiagnostic = "project Dispatch descriptors are invalid";
            return false;
        }
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_RESOURCE_DESC description =
            CD3DX12_RESOURCE_DESC::Tex2D(
                kDispatchOutputFormat,
                width,
                height,
                1u,
                1u,
                1u,
                0u,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const HRESULT created = state.graphics.device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            kDispatchReadState,
            nullptr,
            IID_PPV_ARGS(output.resource.GetAddressOf()));
        if (!HIKARI_DX_CHECK(created, "CreateResource: ProjectDispatchOutput")) {
            outDiagnostic = "could not create project Dispatch output";
            return false;
        }
        GFX::SetD3D12Name(
            output.resource.Get(),
            L"HIKARI Project Dispatch Output");
        state.graphics.resourceStates->Track(
            output.resource.Get(),
            kDispatchReadState);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = kDispatchOutputFormat;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        state.graphics.device->CreateUnorderedAccessView(
            output.resource.Get(), nullptr, &uav, output.outputUav.cpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = kDispatchOutputFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1u;
        state.graphics.device->CreateShaderResourceView(
            output.resource.Get(), &srv, output.outputSrv.cpu);

        RenderResourceDesc poolDesc{};
        poolDesc.usage = RenderResourceUsageFlags::ShaderResource |
            RenderResourceUsageFlags::UnorderedAccess |
            RenderResourceUsageFlags::CopySource;
        poolDesc.lifetime = RenderResourceLifetime::FrameTransient;
        poolDesc.debugName = "ProjectDispatch.Output";
        output.poolHandle = GetRenderResourcePool().RegisterExternal(
            RenderResourceKind::Texture,
            output.resource.Get(),
            std::move(poolDesc));
        if (!output.poolHandle.IsValid() ||
            !GetRenderResourcePool().SetView(
                output.poolHandle,
                RenderResourceViewKind::Srv,
                output.outputSrv) ||
            !GetRenderResourcePool().SetView(
                output.poolHandle,
                RenderResourceViewKind::Uav,
                output.outputUav)) {
            RetireOutput(state, output);
            outDiagnostic = "could not register project Dispatch output";
            return false;
        }
        output.width = width;
        output.height = height;
        return true;
    }

} // namespace

namespace INTERNAL {

bool PrepareProjectDispatchOutput(
    const GFX::Context& graphics,
    const RenderTextureView& input,
    uint32_t width,
    uint32_t height,
    ProjectDispatchOutputBinding& outBinding,
    std::string& outDiagnostic) noexcept {
    outBinding = {};
    try {
        OutputState& state = State();
        if (!PrepareState(state, graphics, outDiagnostic) ||
            !EnsureDescriptorRange(state, outDiagnostic)) {
            return false;
        }
        OutputFrame& output = state.outputs[graphics.frameIndex];
        if (!EnsureOutputResource(
                state, output, width, height, outDiagnostic)) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC inputSrv{};
        inputSrv.Format = input.format;
        inputSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        inputSrv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        inputSrv.Texture2D.MipLevels = 1u;
        graphics.device->CreateShaderResourceView(
            input.resource,
            &inputSrv,
            output.inputSrv.cpu);
        graphics.resourceStates->Transition(
            graphics.cmdList,
            output.resource.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        outBinding.resource = output.resource.Get();
        outBinding.table = output.inputSrv.gpu;
        outBinding.frameSlot = graphics.frameIndex;
        outBinding.width = width;
        outBinding.height = height;
        return true;
    }
    catch (...) {
        outDiagnostic = "project Dispatch output preparation threw";
        return false;
    }
}

bool PublishProjectDispatchOutput(
    const GFX::Context& graphics,
    SURFACE::SceneSurfaceSet& surfaces,
    RenderFrameToken frame,
    ProjectDispatchOutputBinding binding,
    std::string& outDiagnostic) noexcept {
    try {
        OutputState& state = State();
        if (!PrepareState(state, graphics, outDiagnostic) ||
            binding.frameSlot != graphics.frameIndex ||
            binding.frameSlot >= state.outputs.size()) {
            return false;
        }
        OutputFrame& output = state.outputs[binding.frameSlot];
        if (output.resource == nullptr ||
            output.resource.Get() != binding.resource ||
            output.width != binding.width || output.height != binding.height) {
            outDiagnostic = "project Dispatch output binding is stale";
            return false;
        }
        const D3D12_RESOURCE_BARRIER uavBarrier =
            CD3DX12_RESOURCE_BARRIER::UAV(output.resource.Get());
        graphics.cmdList->ResourceBarrier(1u, &uavBarrier);
        graphics.resourceStates->Transition(
            graphics.cmdList,
            output.resource.Get(),
            kDispatchReadState);
        const GRAPH::RenderGraphSurfaceKey key{
            GRAPH::RenderGraphSemantic::ProjectDispatchOutput,
            GRAPH::RenderGraphSemanticPhase::PostOpaque
        };
        (void)surfaces.Invalidate(key);
        const RenderTextureView published{
            output.resource.Get(), output.outputSrv.gpu,
            kDispatchOutputFormat, output.width, output.height, 1u,
            kDispatchReadState, true
        };
        if (!surfaces.PublishTexture(
                key,
                published,
                SURFACE::SceneSurfaceProducer::ProjectDispatch)) {
            outDiagnostic = "could not publish project Dispatch output";
            return false;
        }
        output.publishedFrame = frame;
        GetRenderResourcePool().Touch(output.poolHandle, frame.frameIndex);
        return true;
    }
    catch (...) {
        outDiagnostic = "project Dispatch output publication threw";
        return false;
    }
}

void ReleaseProjectDispatchOutputResources() noexcept {
    OutputState& state = State();
    if (state.ownerThread != std::thread::id{} &&
        state.ownerThread != std::this_thread::get_id()) {
        return;
    }
    RetireState(state, true);
}

} // namespace INTERNAL

bool ResolveProjectDispatchOutput(
    const GFX::Context& graphics,
    const RenderTargetAccess& targetAccess,
    const SURFACE::SceneSurfaceSet& surfaces,
    std::string& outDiagnostic) noexcept {
    try {
        outDiagnostic.clear();
        const GRAPH::RenderGraphSurfaceKey key{
            GRAPH::RenderGraphSemantic::ProjectDispatchOutput,
            GRAPH::RenderGraphSemanticPhase::PostOpaque
        };
        const RenderTextureView* published = surfaces.TryGetTexture(key);
        if (published == nullptr) {
            return true;
        }
        OutputState& state = State();
        if (!PrepareState(state, graphics, outDiagnostic)) {
            return false;
        }
        const OutputFrame& output = state.outputs[graphics.frameIndex];
        if (output.resource == nullptr ||
            output.resource.Get() != published->resource ||
            !output.publishedFrame.Matches(surfaces.GetFrameToken()) ||
            (published->state & D3D12_RESOURCE_STATE_COPY_SOURCE) == 0u ||
            !targetAccess.CopyColorFrom(
                published->resource,
                published->format,
                published->width,
                published->height)) {
            outDiagnostic = "could not resolve project Dispatch output";
            return false;
        }
        return true;
    }
    catch (...) {
        outDiagnostic = "project Dispatch resolve threw an exception";
        return false;
    }
}

} // namespace HIKARI::RENDER3D::PIPELINE
