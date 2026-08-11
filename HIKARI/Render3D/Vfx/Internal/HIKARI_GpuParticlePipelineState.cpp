#include "Render3D/Vfx/Internal/HIKARI_GpuParticlePipelineState.h"

#include <array>

#include <d3dx12.h>

#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleShaderInterop.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {
    namespace {
        using Microsoft::WRL::ComPtr;

        inline constexpr uint32_t kD3d12RootSignatureDwordLimit = 64u;
        inline constexpr uint32_t
            kGpuParticleDrawRootSignatureFixedDwordCost = 12u;
        static_assert(
            kGpuParticleDrawRootSignatureFixedDwordCost +
                sizeof(GpuParticleDrawConstants) / sizeof(uint32_t) <=
                    kD3d12RootSignatureDwordLimit,
            "GPU particle draw root signature exceeds the D3D12 DWORD budget.");

        bool CreateRootSignature(
            ID3D12Device* device,
            const D3D12_ROOT_SIGNATURE_DESC& description,
            const char* diagnosticName,
            ID3D12RootSignature** outSignature) {

            ComPtr<ID3DBlob> signature{};
            ComPtr<ID3DBlob> errors{};
            const HRESULT serializeResult = D3D12SerializeRootSignature(
                &description,
                D3D_ROOT_SIGNATURE_VERSION_1,
                signature.GetAddressOf(),
                errors.GetAddressOf());
            if (!HIKARI_DX_CHECK(serializeResult, diagnosticName)) {
                return false;
            }
            return HIKARI_DX_CHECK(
                device->CreateRootSignature(
                    0u,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(outSignature)),
                diagnosticName);
        }
    }

    bool GpuParticlePipelineState::Initialize(ID3D12Device* device) {
        Shutdown();
        return device != nullptr &&
            CreateComputePipeline(device) &&
            CreateDrawPipeline(device);
    }

    void GpuParticlePipelineState::Shutdown() noexcept {
        for (auto& pipeline : computePipelines_) {
            pipeline.Reset();
        }
        computeRootSignature_.Reset();
        for (auto& blendPipelines : drawPipelines_) {
            for (auto& pipeline : blendPipelines) {
                pipeline.Reset();
            }
        }
        drawRootSignature_.Reset();
    }

    ID3D12RootSignature*
        GpuParticlePipelineState::GetComputeRootSignature() const noexcept {
        return computeRootSignature_.Get();
    }

    ID3D12PipelineState* GpuParticlePipelineState::GetComputePipeline(
        uint32_t pipelineIndex) const noexcept {
        return pipelineIndex < computePipelines_.size()
            ? computePipelines_[pipelineIndex].Get()
            : nullptr;
    }

    ID3D12RootSignature*
        GpuParticlePipelineState::GetDrawRootSignature() const noexcept {
        return drawRootSignature_.Get();
    }

    ID3D12PipelineState* GpuParticlePipelineState::GetDrawPipeline(
        uint32_t blendIndex,
        uint32_t depthIndex) const noexcept {
        return blendIndex < drawPipelines_.size() &&
            depthIndex < drawPipelines_[blendIndex].size()
            ? drawPipelines_[blendIndex][depthIndex].Get()
            : nullptr;
    }

    bool GpuParticlePipelineState::CreateComputePipeline(
        ID3D12Device* device) {
        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1u;
        uavRange.BaseShaderRegister = 0u;
        uavRange.OffsetInDescriptorsFromTableStart = 0u;

        std::array<D3D12_ROOT_PARAMETER, 3u> parameters{};
        parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable.NumDescriptorRanges = 1u;
        parameters[0].DescriptorTable.pDescriptorRanges = &uavRange;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1].Descriptor.ShaderRegister = 0u;
        parameters[2].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants.ShaderRegister = 0u;
        parameters[2].Constants.Num32BitValues =
            sizeof(GpuParticleSimulationConstants) / sizeof(uint32_t);

        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters = static_cast<UINT>(parameters.size());
        rootDescription.pParameters = parameters.data();
        if (!CreateRootSignature(
                device,
                rootDescription,
                "GpuParticleRenderer::CreateComputeRootSignature",
                computeRootSignature_.GetAddressOf())) {
            return false;
        }
        GFX::SetD3D12Name(
            computeRootSignature_.Get(),
            L"HIKARI GPU Particle Compute Root Signature");

        constexpr const char* entries[] = {
            "ResetCS",
            "UpdateCS",
            "SpawnCS"
        };
        for (size_t index = 0u;
             index < computePipelines_.size();
             ++index) {
            ComPtr<ID3DBlob> shader{};
            if (!GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Render3D_GpuParticles.hlsl",
                    entries[index],
                    GFX::ShaderStage::Compute,
                    shader.GetAddressOf())) {
                return false;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
            description.pRootSignature = computeRootSignature_.Get();
            description.CS = {
                shader->GetBufferPointer(),
                shader->GetBufferSize()
            };
            if (!HIKARI_DX_CHECK(
                    device->CreateComputePipelineState(
                        &description,
                        IID_PPV_ARGS(
                            computePipelines_[index].GetAddressOf())),
                    "GpuParticleRenderer::CreateComputePSO")) {
                return false;
            }
        }
        return true;
    }

    bool GpuParticlePipelineState::CreateDrawPipeline(
        ID3D12Device* device) {
        std::array<D3D12_DESCRIPTOR_RANGE, 2u> ranges{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1u;
        ranges[0].BaseShaderRegister = 1u;
        ranges[0].OffsetInDescriptorsFromTableStart = 0u;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[1].NumDescriptors = GFX::DESCRIPTOR::kUserSrvCount;
        ranges[1].BaseShaderRegister = 20u;
        ranges[1].OffsetInDescriptorsFromTableStart = 0u;

        std::array<D3D12_ROOT_PARAMETER, 7u> parameters{};
        parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable.NumDescriptorRanges = 1u;
        parameters[0].DescriptorTable.pDescriptorRanges = &ranges[0];
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1].Descriptor.ShaderRegister = 16u;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[2].Descriptor.ShaderRegister = 0u;
        parameters[2].Descriptor.RegisterSpace = 6u;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[3].Descriptor.ShaderRegister = 1u;
        parameters[3].Descriptor.RegisterSpace = 6u;
        parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[4].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[4].DescriptorTable.NumDescriptorRanges = 1u;
        parameters[4].DescriptorTable.pDescriptorRanges = &ranges[1];
        parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[5].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[5].Constants.ShaderRegister = 0u;
        parameters[5].Constants.Num32BitValues =
            sizeof(GpuParticleDrawConstants) / sizeof(uint32_t);
        parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[6].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[6].Constants.ShaderRegister = 7u;
        parameters[6].Constants.Num32BitValues = 4u;
        parameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0u;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters = static_cast<UINT>(parameters.size());
        rootDescription.pParameters = parameters.data();
        rootDescription.NumStaticSamplers = 1u;
        rootDescription.pStaticSamplers = &sampler;
        rootDescription.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        if (!CreateRootSignature(
                device,
                rootDescription,
                "GpuParticleRenderer::CreateDrawRootSignature",
                drawRootSignature_.GetAddressOf())) {
            return false;
        }
        GFX::SetD3D12Name(
            drawRootSignature_.Get(),
            L"HIKARI GPU Particle Draw Root Signature");

        ComPtr<ID3DBlob> vertexShader{};
        ComPtr<ID3DBlob> pixelShader{};
        if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GpuParticles.hlsl",
                "ParticleVS",
                GFX::ShaderStage::Vertex,
                vertexShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GpuParticles.hlsl",
                "ParticlePS",
                GFX::ShaderStage::Pixel,
                pixelShader.GetAddressOf())) {
            return false;
        }

        for (uint32_t blendIndex = 0u; blendIndex < 2u; ++blendIndex) {
            for (uint32_t depthIndex = 0u; depthIndex < 2u; ++depthIndex) {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
                description.pRootSignature = drawRootSignature_.Get();
                description.VS = {
                    vertexShader->GetBufferPointer(),
                    vertexShader->GetBufferSize()
                };
                description.PS = {
                    pixelShader->GetBufferPointer(),
                    pixelShader->GetBufferSize()
                };
                description.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
                D3D12_RENDER_TARGET_BLEND_DESC& blend =
                    description.BlendState.RenderTarget[0];
                blend.BlendEnable = TRUE;
                blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                blend.DestBlend = blendIndex == 0u
                    ? D3D12_BLEND_INV_SRC_ALPHA
                    : D3D12_BLEND_ONE;
                blend.BlendOp = D3D12_BLEND_OP_ADD;
                blend.SrcBlendAlpha = D3D12_BLEND_ONE;
                blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                description.SampleMask = UINT_MAX;
                description.RasterizerState =
                    CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
                description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                description.DepthStencilState =
                    CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
                description.DepthStencilState.DepthEnable = depthIndex != 0u;
                description.DepthStencilState.DepthWriteMask =
                    D3D12_DEPTH_WRITE_MASK_ZERO;
                description.DepthStencilState.DepthFunc =
                    D3D12_COMPARISON_FUNC_LESS_EQUAL;
                description.PrimitiveTopologyType =
                    D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                description.NumRenderTargets = 1u;
                description.RTVFormats[0] =
                    DXGI_FORMAT_R16G16B16A16_FLOAT;
                description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
                description.SampleDesc.Count = 1u;
                if (!HIKARI_DX_CHECK(
                        device->CreateGraphicsPipelineState(
                            &description,
                            IID_PPV_ARGS(
                                drawPipelines_[blendIndex][depthIndex]
                                    .GetAddressOf())),
                        "GpuParticleRenderer::CreateDrawPSO")) {
                    return false;
                }
            }
        }
        return true;
    }

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
