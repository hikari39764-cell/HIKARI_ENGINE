#include "Render3D/Features/HIKARI_StylizedScreenFeatureRenderer.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"

namespace HIKARI::RENDER3D::FEATURES {

    namespace {
        struct alignas(16) ScreenFeatureConstants {
            MATH::Vec4 screenParams{};
            MATH::Vec4 edgeParams{};
            MATH::Vec4 outlineColor{};
            MATH::Vec4 directionalLight{};
            MATH::Vec4 faceHairParams{};
            uint32_t featureFlags[4]{};
            MATH::Mat4 viewProj{};
        };

        uint32_t BuildScheduledFeatureMask(
            const RenderFeaturePlan& plan) noexcept {
            uint32_t mask = 0u;
            for (const RenderFeaturePlanEntry& entry : plan.entries) {
                if (entry.scheduled) {
                    mask |= RenderFeatureBit(entry.kind);
                }
            }
            return mask;
        }

        uint32_t AlignConstantBufferSize(uint32_t size) noexcept {
            return (size + 255u) & ~255u;
        }
    }

    bool StylizedScreenFeatureRenderer::Render(
        const GFX::Context& graphics,
        const PIPELINE::RenderTargetAccess& targetAccess,
        const SURFACE::SceneSurfaceSet& surfaces,
        const RenderFeaturePlan& plan,
        const StylizedScreenFeatureSettings& settings) {

        debugState_ = {};
        debugState_.requested = plan.screenCompositePassRequired;
        debugState_.transientBytes = plan.transientBytes;
        debugState_.inputsReady =
            surfaces.sceneDepth.IsSampleable() &&
            surfaces.sceneColorPostOpaque.IsSampleable() &&
            surfaces.packedSurface.IsSampleable() &&
            surfaces.HasStylizedFeatureMask();
        debugState_.materialStyleReady =
            surfaces.HasStylizedOutlineStyle();
        debugState_.scheduledFeatureMask =
            BuildScheduledFeatureMask(plan);
        if (!debugState_.requested || !debugState_.inputsReady ||
            graphics.device == nullptr || graphics.cmdList == nullptr ||
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
        ScreenFeatureConstants constants{};
        constants.screenParams = {
            static_cast<float>(surfaces.sceneDepth.width),
            static_cast<float>(surfaces.sceneDepth.height),
            1.0f / static_cast<float>((std::max)(1u, surfaces.sceneDepth.width)),
            1.0f / static_cast<float>((std::max)(1u, surfaces.sceneDepth.height))
        };
        constants.edgeParams = {
            std::clamp(settings.thicknessPixels, 0.25f, 8.0f),
            std::clamp(settings.depthThreshold, 0.00001f, 0.25f),
            std::clamp(settings.normalThreshold, 0.001f, 1.0f),
            std::clamp(settings.intensity, 0.0f, 1.0f)
        };
        constants.outlineColor = {
            (std::max)(0.0f, settings.outlineColor[0]),
            (std::max)(0.0f, settings.outlineColor[1]),
            (std::max)(0.0f, settings.outlineColor[2]),
            1.0f
        };
        constants.directionalLight = {
            settings.directionalLightWS[0],
            settings.directionalLightWS[1],
            settings.directionalLightWS[2],
            0.0f
        };
        constants.faceHairParams = {
            std::clamp(settings.faceHairShadowScale, 0.0f, 2.0f),
            std::clamp(settings.faceHairDepthTolerance, 0.00001f, 0.1f),
            0.0f,
            0.0f
        };
        constants.featureFlags[0] = debugState_.scheduledFeatureMask;
        constants.featureFlags[1] =
            plan.entries[static_cast<size_t>(
                RenderFeatureKind::GeometryOutline)].compatibilityFallback
                ? 1u
                : 0u;
        constants.featureFlags[2] =
            debugState_.materialStyleReady ? 1u : 0u;
        constants.viewProj = settings.viewProj;
        std::memcpy(
            constantMapped_ + static_cast<size_t>(constantStride_) * frameSlot,
            &constants,
            sizeof(constants));

        if (!targetAccess.Rebind()) {
            return false;
        }
        ID3D12GraphicsCommandList* cmd = graphics.cmdList;
        GFX::PIX::ScopedGpuEvent event(
            cmd, GFX::PIX::kColorPost, "RenderFeature.StylizedComposite");
        GFX::GPU_PROFILE::ScopedGpuTimer timer(
            cmd, GFX::GPU_PROFILE::Pass::StylizedScreenFeatures);
        ID3D12DescriptorHeap* heaps[]{ graphics.srvHeap };
        cmd->SetDescriptorHeaps(1u, heaps);
        cmd->SetGraphicsRootSignature(rootSignature_.Get());
        cmd->SetPipelineState(pipelineState_.Get());
        cmd->SetGraphicsRootConstantBufferView(
            0u,
            constantBuffer_->GetGPUVirtualAddress() +
                static_cast<UINT64>(constantStride_) * frameSlot);
        cmd->SetGraphicsRootDescriptorTable(
            1u,
            surfaces.sceneColorPostOpaque.srv);
        cmd->SetGraphicsRootDescriptorTable(2u, surfaces.sceneDepth.srv);
        cmd->SetGraphicsRootDescriptorTable(3u, surfaces.packedSurface.srv);
        cmd->SetGraphicsRootDescriptorTable(
            4u, surfaces.surfaceFeatureMask.srv);
        cmd->SetGraphicsRootDescriptorTable(
            5u,
            debugState_.materialStyleReady
                ? surfaces.stylizedOutlineStyle.srv
                : surfaces.packedSurface.srv);
        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(surfaces.sceneDepth.width);
        viewport.Height = static_cast<float>(surfaces.sceneDepth.height);
        viewport.MaxDepth = 1.0f;
        const D3D12_RECT scissor{
            0, 0,
            static_cast<LONG>(surfaces.sceneDepth.width),
            static_cast<LONG>(surfaces.sceneDepth.height)
        };
        cmd->RSSetViewports(1u, &viewport);
        cmd->RSSetScissorRects(1u, &scissor);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0u, 0u, nullptr);
        cmd->IASetIndexBuffer(nullptr);
        cmd->DrawInstanced(3u, 1u, 0u, 0u);
        debugState_.rendered = true;
        debugState_.passCount = 1u;
        return true;
    }

    bool StylizedScreenFeatureRenderer::EnsurePipeline(ID3D12Device* device) {
        if (rootSignature_ != nullptr && pipelineState_ != nullptr) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }

        std::array<D3D12_DESCRIPTOR_RANGE, 5u> ranges{};
        std::array<D3D12_ROOT_PARAMETER, 6u> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[0].Descriptor.ShaderRegister = 0u;
        for (uint32_t index = 0u; index < ranges.size(); ++index) {
            ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[index].NumDescriptors = 1u;
            ranges[index].BaseShaderRegister = index;
            ranges[index].OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            parameters[index + 1u].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[index + 1u].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[index + 1u].DescriptorTable.NumDescriptorRanges = 1u;
            parameters[index + 1u].DescriptorTable.pDescriptorRanges =
                &ranges[index];
        }
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0u;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = static_cast<UINT>(parameters.size());
        rootDesc.pParameters = parameters.data();
        rootDesc.NumStaticSamplers = 1u;
        rootDesc.pStaticSamplers = &sampler;
        rootDesc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Microsoft::WRL::ComPtr<ID3DBlob> signature{};
        Microsoft::WRL::ComPtr<ID3DBlob> errors{};
        if (!HIKARI_DX_CHECK(
                D3D12SerializeRootSignature(
                    &rootDesc,
                    D3D_ROOT_SIGNATURE_VERSION_1,
                    signature.GetAddressOf(),
                    errors.GetAddressOf()),
                "StylizedScreenFeatures::SerializeRootSignature") ||
            !HIKARI_DX_CHECK(
                device->CreateRootSignature(
                    0u,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(rootSignature_.GetAddressOf())),
                "StylizedScreenFeatures::CreateRootSignature")) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader{};
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader{};
        if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Post_StylizedScreenFeatures.hlsl",
                "VSMain", GFX::ShaderStage::Vertex,
                vertexShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Post_StylizedScreenFeatures.hlsl",
                "PSMain", GFX::ShaderStage::Pixel,
                pixelShader.GetAddressOf())) {
            DEBUGLOG::PushRenderError(
                "[RenderFeature][ERROR] screen feature shader compile failed.");
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSignature_.Get();
        desc.VS = { vertexShader->GetBufferPointer(),
                    vertexShader->GetBufferSize() };
        desc.PS = { pixelShader->GetBufferPointer(),
                    pixelShader->GetBufferSize() };
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.InputLayout = { nullptr, 0u };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1u;
        desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1u;
        return HIKARI_DX_CHECK(
            device->CreateGraphicsPipelineState(
                &desc, IID_PPV_ARGS(pipelineState_.GetAddressOf())),
            "StylizedScreenFeatures::CreatePipelineState");
    }

    bool StylizedScreenFeatureRenderer::EnsureConstantBuffer(
        ID3D12Device* device) {
        if (constantBuffer_ != nullptr && constantMapped_ != nullptr) {
            return true;
        }
        constantStride_ = AlignConstantBufferSize(
            sizeof(ScreenFeatureConstants));
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(
            static_cast<UINT64>(constantStride_) * GFX::kFrameResourceCount);
        if (!HIKARI_DX_CHECK(
                device->CreateCommittedResource(
                    &heap, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(constantBuffer_.GetAddressOf())),
                "StylizedScreenFeatures::CreateConstantBuffer") ||
            !HIKARI_DX_CHECK(
                constantBuffer_->Map(
                    0u, nullptr,
                    reinterpret_cast<void**>(&constantMapped_)),
                "StylizedScreenFeatures::MapConstantBuffer")) {
            constantBuffer_.Reset();
            constantMapped_ = nullptr;
            return false;
        }
        return true;
    }

    void StylizedScreenFeatureRenderer::Release() {
        if (constantBuffer_ != nullptr && constantMapped_ != nullptr) {
            constantBuffer_->Unmap(0u, nullptr);
            constantMapped_ = nullptr;
        }
        GFX::RetireD3D12ObjectForCurrentFrame(
            constantBuffer_, "RenderFeature.ConstantBuffer");
        GFX::RetireD3D12ObjectForCurrentFrame(
            pipelineState_, "RenderFeature.ScreenOutlinePSO");
        GFX::RetireD3D12ObjectForCurrentFrame(
            rootSignature_, "RenderFeature.ScreenOutlineRootSignature");
        constantStride_ = 0u;
        debugState_ = {};
    }

} // namespace HIKARI::RENDER3D::FEATURES
