#include "Render3D/Reflection/HIKARI_ScreenSpaceReflectionRenderer.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"

namespace HIKARI::RENDER3D::REFLECTION {

    namespace {
        struct alignas(16) ScreenSpaceReflectionConstants {
            MATH::Mat4 viewProj{};
            MATH::Mat4 invViewProj{};
            MATH::Vec4 cameraPosition{};
            MATH::Vec4 screenParams{};
            MATH::Vec4 traceParams{};
            MATH::Vec4 resolveParams{};
            MATH::Vec4 frameParams{};
        };

        static_assert(sizeof(ScreenSpaceReflectionConstants) == 208u);

        uint32_t AlignConstantBufferSize(uint32_t size) {
            return (size + 255u) & ~255u;
        }

        uint32_t ResolveMaxSteps(ScreenSpaceReflectionQuality quality) {
            switch (quality) {
            case ScreenSpaceReflectionQuality::Low:
                return 24u;
            case ScreenSpaceReflectionQuality::High:
                return 64u;
            case ScreenSpaceReflectionQuality::Medium:
                return 40u;
            case ScreenSpaceReflectionQuality::Off:
            default:
                return 0u;
            }
        }

        uint32_t ResolveDebugMode(RenderDebugView debugView) {
            if (debugView == RenderDebugView::SsrConfidence) {
                return 1u;
            }
            if (debugView == RenderDebugView::SsrHit) {
                return 2u;
            }
            return 0u;
        }
    }

    bool ScreenSpaceReflectionRenderer::Render(
        const GFX::Context& graphics,
        const PIPELINE::RenderTargetAccess& targetAccess,
        const SURFACE::SceneSurfaceSet& surfaces,
        const MESHRENDERER::CameraCB& camera,
        const ScreenSpaceReflectionSettings& settings,
        RenderDebugView debugView) {

        debugState_ = {};
        debugState_.requested =
            settings.enabled &&
            settings.quality != ScreenSpaceReflectionQuality::Off;
        debugState_.sceneSurfaceReady = surfaces.HasOpaqueSurface();
        debugState_.currentHzbReady = surfaces.HasCurrentDepthHierarchy();
        debugState_.hzbMipCount =
            surfaces.currentSceneDepthPyramid.mipCount;
        debugState_.maxDistance = settings.maxDistance;
        debugState_.maxRoughness = settings.maxRoughness;
        debugState_.maxSteps = ResolveMaxSteps(settings.quality);

        if (!debugState_.requested ||
            !debugState_.sceneSurfaceReady ||
            !debugState_.currentHzbReady ||
            graphics.device == nullptr ||
            graphics.cmdList == nullptr ||
            graphics.srvHeap == nullptr) {
            return false;
        }
        if (!EnsurePipeline(graphics.device) ||
            !EnsureConstantBuffer(graphics.device)) {
            return false;
        }
        debugState_.pipelineReady = true;

        const uint32_t frameSlot =
            graphics.frameIndex % GFX::kFrameResourceCount;
        ScreenSpaceReflectionConstants constants{};
        constants.viewProj = camera.viewProj;
        constants.invViewProj = camera.invViewProj;
        constants.cameraPosition = camera.cameraPos;
        constants.screenParams = {
            static_cast<float>(surfaces.sceneDepth.width),
            static_cast<float>(surfaces.sceneDepth.height),
            1.0f / static_cast<float>(
                (std::max)(1u, surfaces.sceneDepth.width)),
            1.0f / static_cast<float>(
                (std::max)(1u, surfaces.sceneDepth.height))
        };
        constants.traceParams = {
            std::clamp(settings.maxDistance, 1.0f, 200.0f),
            std::clamp(settings.thickness, 0.0001f, 0.05f),
            std::clamp(settings.intensity, 0.0f, 2.0f),
            std::clamp(settings.maxRoughness, 0.05f, 1.0f)
        };
        constants.resolveParams = {
            std::clamp(settings.stride, 0.02f, 2.0f),
            static_cast<float>(debugState_.maxSteps),
            static_cast<float>(
                (std::max)(
                    1u,
                    surfaces.currentSceneDepthPyramid.mipCount)),
            std::clamp(settings.edgeFade, 0.01f, 0.35f)
        };
        constants.frameParams = {
            static_cast<float>(ResolveDebugMode(debugView)),
            static_cast<float>(graphics.frameIndex & 0xffffu),
            0.0f,
            0.0f
        };
        std::memcpy(
            constantMapped_ +
                static_cast<size_t>(constantStride_) * frameSlot,
            &constants,
            sizeof(constants));

        if (!targetAccess.Rebind()) {
            return false;
        }

        ID3D12GraphicsCommandList* cmd = graphics.cmdList;
        GFX::PIX::ScopedGpuEvent pixEvent(
            cmd,
            GFX::PIX::kColorPost,
            "ScreenSpaceReflections");
        GFX::GPU_PROFILE::ScopedGpuTimer gpuTimer(
            cmd,
            GFX::GPU_PROFILE::Pass::ScreenSpaceReflections);

        ID3D12DescriptorHeap* heaps[] = { graphics.srvHeap };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootSignature(rootSignature_.Get());
        cmd->SetPipelineState(pipelineState_.Get());
        cmd->SetGraphicsRootConstantBufferView(
            0,
            constantBuffer_->GetGPUVirtualAddress() +
                static_cast<UINT64>(constantStride_) * frameSlot);
        cmd->SetGraphicsRootDescriptorTable(
            1,
            surfaces.opaqueSceneColor.srv);
        cmd->SetGraphicsRootDescriptorTable(
            2,
            surfaces.sceneDepth.srv);
        cmd->SetGraphicsRootDescriptorTable(
            3,
            surfaces.packedSurface.srv);
        cmd->SetGraphicsRootDescriptorTable(
            4,
            surfaces.currentSceneDepthPyramid.pyramidSrv);
        cmd->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0, 0, nullptr);
        cmd->IASetIndexBuffer(nullptr);
        cmd->DrawInstanced(3, 1, 0, 0);
        debugState_.rendered = true;
        return true;
    }

    bool ScreenSpaceReflectionRenderer::EnsurePipeline(
        ID3D12Device* device) {
        if (rootSignature_ != nullptr && pipelineState_ != nullptr) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }

        std::array<D3D12_DESCRIPTOR_RANGE, 4> ranges{};
        std::array<D3D12_ROOT_PARAMETER, 5> parameters{};
        parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[0].Descriptor.ShaderRegister = 0u;
        for (uint32_t index = 0; index < ranges.size(); ++index) {
            ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[index].NumDescriptors = 1u;
            ranges[index].BaseShaderRegister = index;
            ranges[index].OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            parameters[index + 1u].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[index + 1u].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[index + 1u].DescriptorTable.NumDescriptorRanges =
                1u;
            parameters[index + 1u].DescriptorTable.pDescriptorRanges =
                &ranges[index];
        }

        D3D12_STATIC_SAMPLER_DESC linearSampler{};
        linearSampler.Filter =
            D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        linearSampler.AddressU =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        linearSampler.AddressV =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        linearSampler.AddressW =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        linearSampler.ComparisonFunc =
            D3D12_COMPARISON_FUNC_ALWAYS;
        linearSampler.MaxLOD = D3D12_FLOAT32_MAX;
        linearSampler.ShaderRegister = 0u;
        linearSampler.ShaderVisibility =
            D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC pointSampler = linearSampler;
        pointSampler.Filter =
            D3D12_FILTER_MIN_MAG_MIP_POINT;
        pointSampler.ShaderRegister = 1u;
        const D3D12_STATIC_SAMPLER_DESC samplers[] = {
            linearSampler,
            pointSampler
        };

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters =
            static_cast<UINT>(parameters.size());
        rootDesc.pParameters = parameters.data();
        rootDesc.NumStaticSamplers =
            static_cast<UINT>(std::size(samplers));
        rootDesc.pStaticSamplers = samplers;
        rootDesc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT serializeResult =
            D3D12SerializeRootSignature(
                &rootDesc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                signature.GetAddressOf(),
                errors.GetAddressOf());
        if (!HIKARI_DX_CHECK(
                serializeResult,
                "ScreenSpaceReflectionRenderer::SerializeRootSignature")) {
            return false;
        }
        if (!HIKARI_DX_CHECK(
                device->CreateRootSignature(
                    0u,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(rootSignature_.GetAddressOf())),
                "ScreenSpaceReflectionRenderer::CreateRootSignature")) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Post_ScreenSpaceReflections.hlsl",
                "VSMain",
                GFX::ShaderStage::Vertex,
                vertexShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Post_ScreenSpaceReflections.hlsl",
                "PSMain",
                GFX::ShaderStage::Pixel,
                pixelShader.GetAddressOf())) {
            DEBUGLOG::PushRenderError(
                "[SSR][ERROR] shader compilation failed.");
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
        pipelineDesc.pRootSignature = rootSignature_.Get();
        pipelineDesc.VS = {
            vertexShader->GetBufferPointer(),
            vertexShader->GetBufferSize()
        };
        pipelineDesc.PS = {
            pixelShader->GetBufferPointer(),
            pixelShader->GetBufferSize()
        };
        pipelineDesc.BlendState =
            CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pipelineDesc.SampleMask = UINT_MAX;
        pipelineDesc.RasterizerState =
            CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pipelineDesc.RasterizerState.CullMode =
            D3D12_CULL_MODE_NONE;
        pipelineDesc.DepthStencilState =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        pipelineDesc.DepthStencilState.DepthEnable = FALSE;
        pipelineDesc.DepthStencilState.DepthWriteMask =
            D3D12_DEPTH_WRITE_MASK_ZERO;
        pipelineDesc.InputLayout = { nullptr, 0u };
        pipelineDesc.PrimitiveTopologyType =
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineDesc.NumRenderTargets = 1u;
        pipelineDesc.RTVFormats[0] =
            DXGI_FORMAT_R16G16B16A16_FLOAT;
        pipelineDesc.SampleDesc.Count = 1u;
        return HIKARI_DX_CHECK(
            device->CreateGraphicsPipelineState(
                &pipelineDesc,
                IID_PPV_ARGS(pipelineState_.GetAddressOf())),
            "ScreenSpaceReflectionRenderer::CreatePipelineState");
    }

    bool ScreenSpaceReflectionRenderer::EnsureConstantBuffer(
        ID3D12Device* device) {
        if (constantBuffer_ != nullptr && constantMapped_ != nullptr) {
            return true;
        }
        constantStride_ = AlignConstantBufferSize(
            sizeof(ScreenSpaceReflectionConstants));
        const UINT64 bufferSize =
            static_cast<UINT64>(constantStride_) *
            GFX::kFrameResourceCount;
        const auto heapProperties =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto resourceDesc =
            CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        if (!HIKARI_DX_CHECK(
                device->CreateCommittedResource(
                    &heapProperties,
                    D3D12_HEAP_FLAG_NONE,
                    &resourceDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(constantBuffer_.GetAddressOf())),
                "ScreenSpaceReflectionRenderer::CreateConstantBuffer")) {
            return false;
        }
        if (!HIKARI_DX_CHECK(
                constantBuffer_->Map(
                    0u,
                    nullptr,
                    reinterpret_cast<void**>(&constantMapped_)),
                "ScreenSpaceReflectionRenderer::MapConstantBuffer")) {
            constantBuffer_.Reset();
            return false;
        }
        constantBuffer_->SetName(L"HIKARI.SSR.Constants");
        return true;
    }

    void ScreenSpaceReflectionRenderer::Release() {
        if (constantBuffer_ != nullptr && constantMapped_ != nullptr) {
            constantBuffer_->Unmap(0u, nullptr);
            constantMapped_ = nullptr;
        }
        GFX::RetireD3D12ObjectForCurrentFrame(
            constantBuffer_,
            "SSR.ConstantBuffer");
        GFX::RetireD3D12ObjectForCurrentFrame(
            pipelineState_,
            "SSR.PipelineState");
        GFX::RetireD3D12ObjectForCurrentFrame(
            rootSignature_,
            "SSR.RootSignature");
        constantStride_ = 0u;
        debugState_ = {};
    }

} // namespace HIKARI::RENDER3D::REFLECTION
