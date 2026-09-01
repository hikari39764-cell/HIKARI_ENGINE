#include "Render3D/Vfx/Internal/HIKARI_ParticleGpuDrivenPipelineState.h"

#include <array>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include <d3dx12.h>

#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Assets/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleShaderInterop.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {
    namespace {
        using Microsoft::WRL::ComPtr;

        struct ParticlePipelineStateStream {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_MS meshShader;
            CD3DX12_PIPELINE_STATE_STREAM_PS pixelShader;
            CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blend;
            CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depthStencil;
            CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsvFormat;
            CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtvFormats;
            CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC sampleDescription;
            CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK sampleMask;
        };

        bool CreateRootSignature(
            ID3D12Device* device,
            const D3D12_ROOT_SIGNATURE_DESC& description,
            const char* diagnosticName,
            ID3D12RootSignature** output) {
            ComPtr<ID3DBlob> signature{};
            ComPtr<ID3DBlob> errors{};
            if (!HIKARI_DX_CHECK(
                    D3D12SerializeRootSignature(
                        &description,
                        D3D_ROOT_SIGNATURE_VERSION_1,
                        signature.GetAddressOf(),
                        errors.GetAddressOf()),
                    diagnosticName)) {
                return false;
            }
            return HIKARI_DX_CHECK(
                device->CreateRootSignature(
                    0u,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(output)),
                diagnosticName);
        }

        bool CreateComputePipeline(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            const ASSETS::SHADER::ShaderAssetContractExpectation& contract,
            const char* programId,
            const char* entry,
            ID3D12PipelineState** output) {
            ASSETS::SHADER::ShaderAssetResolveResult resolved =
                ASSETS::SHADER::ResolveBuiltinShaderEntry(
                    programId,
                    { entry,
                      ASSETS::SHADER::ShaderStage::Compute,
                      "cs_6_0" },
                    contract);
            std::string diagnostic{};
            const ASSETS::SHADER::ShaderCookedArtifact* artifact =
                resolved ? resolved.lease.GetArtifact() : nullptr;
            if (!resolved || artifact == nullptr ||
                !ASSETS::SHADER::ValidateRenderGeometryShaderArtifact(
                    *artifact,
                    diagnostic)) {
                if (diagnostic.empty()) {
                    diagnostic = resolved.diagnostic;
                }
                HIKARI_LOG_ERROR(
                    std::string("[ParticleGpuDriven][Shader] ") +
                    programId + ": " + diagnostic);
                return false;
            }
            const std::span<const uint8_t> shader =
                resolved.lease.GetDxil();
            if (shader.empty()) {
                return false;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
            description.pRootSignature = rootSignature;
            description.CS = { shader.data(), shader.size() };
            return HIKARI_DX_CHECK(
                device->CreateComputePipelineState(
                    &description,
                    IID_PPV_ARGS(output)),
                "ParticleGpuDrivenPass::CreateComputePipeline");
        }
    }

    bool ParticleGpuDrivenPipelineState::Initialize(
        ID3D12Device* device) {
        Shutdown();
        return device != nullptr &&
            CreateSimulationPipelines(device) &&
            CreateCompactionPipelines(device) &&
            CreateDrawPipelines(device);
    }

    void ParticleGpuDrivenPipelineState::Shutdown() noexcept {
        for (auto& pipeline : simulationPipelines_) pipeline.Reset();
        for (auto& pipeline : compactionPipelines_) pipeline.Reset();
        for (auto& shapePipelines : drawPipelines_) {
            for (auto& blendPipelines : shapePipelines) {
                for (auto& pipeline : blendPipelines) pipeline.Reset();
            }
        }
        simulationRootSignature_.Reset();
        compactionRootSignature_.Reset();
        drawRootSignature_.Reset();
        dispatchMeshCommandSignature_.Reset();
    }

    ID3D12RootSignature*
        ParticleGpuDrivenPipelineState::GetSimulationRootSignature()
            const noexcept {
        return simulationRootSignature_.Get();
    }

    ID3D12PipelineState*
        ParticleGpuDrivenPipelineState::GetSimulationPipeline(
            uint32_t pipelineIndex) const noexcept {
        return pipelineIndex < simulationPipelines_.size()
            ? simulationPipelines_[pipelineIndex].Get()
            : nullptr;
    }

    ID3D12RootSignature*
        ParticleGpuDrivenPipelineState::GetCompactionRootSignature()
            const noexcept {
        return compactionRootSignature_.Get();
    }

    ID3D12PipelineState*
        ParticleGpuDrivenPipelineState::GetCompactionPipeline(
            uint32_t pipelineIndex) const noexcept {
        return pipelineIndex < compactionPipelines_.size()
            ? compactionPipelines_[pipelineIndex].Get()
            : nullptr;
    }

    ID3D12RootSignature*
        ParticleGpuDrivenPipelineState::GetDrawRootSignature()
            const noexcept {
        return drawRootSignature_.Get();
    }

    ID3D12PipelineState*
        ParticleGpuDrivenPipelineState::GetDrawPipeline(
            uint32_t shapeIndex,
            uint32_t blendIndex,
            uint32_t depthIndex) const noexcept {
        return shapeIndex < drawPipelines_.size() &&
            blendIndex < drawPipelines_[shapeIndex].size() &&
            depthIndex < drawPipelines_[shapeIndex][blendIndex].size()
            ? drawPipelines_[shapeIndex][blendIndex][depthIndex].Get()
            : nullptr;
    }

    ID3D12CommandSignature*
        ParticleGpuDrivenPipelineState::GetDispatchMeshCommandSignature()
            const noexcept {
        return dispatchMeshCommandSignature_.Get();
    }

    bool ParticleGpuDrivenPipelineState::CreateSimulationPipelines(
        ID3D12Device* device) {
        D3D12_DESCRIPTOR_RANGE particleUav{};
        particleUav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        particleUav.NumDescriptors = 1u;
        particleUav.BaseShaderRegister = 0u;

        std::array<D3D12_ROOT_PARAMETER, 3u> parameters{};
        parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable.NumDescriptorRanges = 1u;
        parameters[0].DescriptorTable.pDescriptorRanges = &particleUav;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1].Descriptor.ShaderRegister = 0u;
        parameters[2].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants.ShaderRegister = 0u;
        parameters[2].Constants.Num32BitValues =
            sizeof(GpuParticleSimulationConstants) / sizeof(uint32_t);

        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = static_cast<UINT>(parameters.size());
        description.pParameters = parameters.data();
        if (!CreateRootSignature(
                device,
                description,
                "ParticleGpuDrivenPass::CreateSimulationRootSignature",
                simulationRootSignature_.GetAddressOf())) {
            return false;
        }
        const GFX::ShaderDeviceCapabilities capabilities =
            GFX::QueryShaderDeviceCapabilities(device);
        const ASSETS::SHADER::ShaderAssetContractExpectation shaderContract =
            ASSETS::SHADER::MakeRenderGeometryShaderContract(
                ASSETS::SHADER::RenderGeometryRootLayout::
                    ParticleSimulation,
                { capabilities.shaderModel,
                  capabilities.rootSignatureVersion,
                  capabilities.featureMask });
        constexpr std::array entries{"ResetCS", "UpdateCS", "SpawnCS"};
        for (size_t index = 0u; index < entries.size(); ++index) {
            if (!CreateComputePipeline(
                    device,
                    simulationRootSignature_.Get(),
                    shaderContract,
                    "HIKARI.EngineShader.Render3D_ParticleSimulation",
                    entries[index],
                    simulationPipelines_[index].GetAddressOf())) {
                return false;
            }
        }
        return true;
    }

    bool ParticleGpuDrivenPipelineState::CreateCompactionPipelines(
        ID3D12Device* device) {
        D3D12_DESCRIPTOR_RANGE particleUav{};
        particleUav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        particleUav.NumDescriptors = 1u;
        particleUav.BaseShaderRegister = 0u;

        std::array<D3D12_ROOT_PARAMETER, 5u> parameters{};
        parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable.NumDescriptorRanges = 1u;
        parameters[0].DescriptorTable.pDescriptorRanges = &particleUav;
        for (uint32_t index = 1u; index <= 3u; ++index) {
            parameters[index].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_UAV;
            parameters[index].Descriptor.ShaderRegister = index;
        }
        parameters[4].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[4].Constants.ShaderRegister = 1u;
        parameters[4].Constants.Num32BitValues =
            sizeof(GpuParticleCompactConstants) / sizeof(uint32_t);

        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = static_cast<UINT>(parameters.size());
        description.pParameters = parameters.data();
        if (!CreateRootSignature(
                device,
                description,
                "ParticleGpuDrivenPass::CreateCompactionRootSignature",
                compactionRootSignature_.GetAddressOf())) {
            return false;
        }
        const GFX::ShaderDeviceCapabilities capabilities =
            GFX::QueryShaderDeviceCapabilities(device);
        const ASSETS::SHADER::ShaderAssetContractExpectation shaderContract =
            ASSETS::SHADER::MakeRenderGeometryShaderContract(
                ASSETS::SHADER::RenderGeometryRootLayout::
                    ParticleCompaction,
                { capabilities.shaderModel,
                  capabilities.rootSignatureVersion,
                  capabilities.featureMask });
        constexpr std::array entries{
            "ResetDrawCS", "CompactDrawCS", "BuildDrawArgsCS"};
        for (size_t index = 0u; index < entries.size(); ++index) {
            if (!CreateComputePipeline(
                    device,
                    compactionRootSignature_.Get(),
                    shaderContract,
                    "HIKARI.EngineShader."
                    "Render3D_ParticleGpuDrivenCompute",
                    entries[index],
                    compactionPipelines_[index].GetAddressOf())) {
                return false;
            }
        }
        return true;
    }

    bool ParticleGpuDrivenPipelineState::CreateDrawPipelines(
        ID3D12Device* device) {
        D3D12_DESCRIPTOR_RANGE textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = GFX::DESCRIPTOR::kUserSrvCount;
        textureRange.BaseShaderRegister = 20u;

        D3D12_DESCRIPTOR_RANGE sceneDepthRange{};
        sceneDepthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        sceneDepthRange.NumDescriptors = 1u;
        sceneDepthRange.BaseShaderRegister = 3u;

        std::array<D3D12_ROOT_PARAMETER, 10u> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].Descriptor.ShaderRegister = 0u;
        for (uint32_t index = 1u; index <= 5u; ++index) {
            parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        }
        parameters[1].Descriptor.ShaderRegister = 1u;
        parameters[2].Descriptor.ShaderRegister = 2u;
        parameters[3].Descriptor.ShaderRegister = 16u;
        parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[4].Descriptor.ShaderRegister = 0u;
        parameters[4].Descriptor.RegisterSpace = 6u;
        parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[5].Descriptor.ShaderRegister = 1u;
        parameters[5].Descriptor.RegisterSpace = 6u;
        parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[6].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[6].DescriptorTable.NumDescriptorRanges = 1u;
        parameters[6].DescriptorTable.pDescriptorRanges = &textureRange;
        parameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[7].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[7].Constants.ShaderRegister = 1u;
        parameters[7].Constants.Num32BitValues =
            sizeof(GpuParticleDrawConstants) / sizeof(uint32_t);
        parameters[8].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[8].Constants.ShaderRegister = 7u;
        parameters[8].Constants.Num32BitValues = 4u;
        parameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[9].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[9].DescriptorTable.NumDescriptorRanges = 1u;
        parameters[9].DescriptorTable.pDescriptorRanges = &sceneDepthRange;
        parameters[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        std::array<D3D12_STATIC_SAMPLER_DESC, 2u> samplers{};
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[0].ShaderRegister = 0u;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samplers[1] = samplers[0];
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[1].ShaderRegister = 1u;

        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters = static_cast<UINT>(parameters.size());
        rootDescription.pParameters = parameters.data();
        rootDescription.NumStaticSamplers =
            static_cast<UINT>(samplers.size());
        rootDescription.pStaticSamplers = samplers.data();
        if (!CreateRootSignature(
                device,
                rootDescription,
                "ParticleGpuDrivenPass::CreateDrawRootSignature",
                drawRootSignature_.GetAddressOf())) {
            return false;
        }

        const GFX::ShaderDeviceCapabilities capabilities =
            GFX::QueryShaderDeviceCapabilities(device);
        const ASSETS::SHADER::ShaderAssetContractExpectation shaderContract =
            ASSETS::SHADER::MakeRenderGeometryShaderContract(
                ASSETS::SHADER::RenderGeometryRootLayout::ParticleDraw,
                { capabilities.shaderModel,
                  capabilities.rootSignatureVersion,
                  capabilities.featureMask });
        const auto resolveShader = [&shaderContract](
            const char* entry,
            ASSETS::SHADER::ShaderStage stage,
            const char* profile,
            ASSETS::SHADER::ShaderAssetEntryLease& outShader) {
            ASSETS::SHADER::ShaderAssetResolveResult resolved =
                ASSETS::SHADER::ResolveBuiltinShaderEntry(
                    "HIKARI.EngineShader.Render3D_ParticleGpuDrivenDraw",
                    { entry, stage, profile },
                    shaderContract);
            std::string diagnostic{};
            const ASSETS::SHADER::ShaderCookedArtifact* artifact =
                resolved ? resolved.lease.GetArtifact() : nullptr;
            if (!resolved || artifact == nullptr ||
                !ASSETS::SHADER::ValidateRenderGeometryShaderArtifact(
                    *artifact,
                    diagnostic) ||
                resolved.lease.GetDxil().empty()) {
                if (diagnostic.empty()) {
                    diagnostic = resolved.diagnostic;
                }
                HIKARI_LOG_ERROR(
                    std::string("[ParticleGpuDriven][Shader] ") +
                    entry + ": " + diagnostic);
                return false;
            }
            outShader = std::move(resolved.lease);
            return true;
        };
        std::array<ASSETS::SHADER::ShaderAssetEntryLease, 2u>
            meshShaders{};
        ASSETS::SHADER::ShaderAssetEntryLease pixelShader{};
        if (!resolveShader(
                "ParticleSpriteMS",
                ASSETS::SHADER::ShaderStage::Mesh,
                "ms_6_5",
                meshShaders[0u]) ||
            !resolveShader(
                "ParticleStripMS",
                ASSETS::SHADER::ShaderStage::Mesh,
                "ms_6_5",
                meshShaders[1u]) ||
            !resolveShader(
                "ParticlePS",
                ASSETS::SHADER::ShaderStage::Pixel,
                "ps_6_0",
                pixelShader)) {
            return false;
        }

        ComPtr<ID3D12Device2> device2{};
        if (!HIKARI_DX_CHECK(
                device->QueryInterface(IID_PPV_ARGS(device2.GetAddressOf())),
                "ParticleGpuDrivenPass::QueryDevice2")) {
            return false;
        }
        for (uint32_t shapeIndex = 0u; shapeIndex < 2u; ++shapeIndex) {
          for (uint32_t blendIndex = 0u; blendIndex < 2u; ++blendIndex) {
            for (uint32_t depthIndex = 0u; depthIndex < 2u; ++depthIndex) {
                CD3DX12_BLEND_DESC blend(D3D12_DEFAULT);
                auto& target = blend.RenderTarget[0];
                target.BlendEnable = TRUE;
                target.SrcBlend = D3D12_BLEND_ONE;
                target.DestBlend = blendIndex == 0u
                    ? D3D12_BLEND_INV_SRC_ALPHA
                    : D3D12_BLEND_ONE;
                target.BlendOp = D3D12_BLEND_OP_ADD;
                target.SrcBlendAlpha = D3D12_BLEND_ONE;
                target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                target.BlendOpAlpha = D3D12_BLEND_OP_ADD;

                CD3DX12_DEPTH_STENCIL_DESC depth(D3D12_DEFAULT);
                depth.DepthEnable = depthIndex != 0u;
                depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
                depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
                CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
                rasterizer.CullMode = D3D12_CULL_MODE_NONE;
                D3D12_RT_FORMAT_ARRAY formats{};
                formats.NumRenderTargets = 1u;
                formats.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

                ParticlePipelineStateStream stream{};
                stream.rootSignature = drawRootSignature_.Get();
                const std::span<const uint8_t> meshShader =
                    meshShaders[shapeIndex].GetDxil();
                const std::span<const uint8_t> pixelShaderBytecode =
                    pixelShader.GetDxil();
                stream.meshShader = CD3DX12_SHADER_BYTECODE(
                    meshShader.data(), meshShader.size());
                stream.pixelShader = CD3DX12_SHADER_BYTECODE(
                    pixelShaderBytecode.data(),
                    pixelShaderBytecode.size());
                stream.blend = blend;
                stream.depthStencil = depth;
                stream.dsvFormat = DXGI_FORMAT_D32_FLOAT;
                stream.rasterizer = rasterizer;
                stream.rtvFormats = formats;
                stream.sampleDescription = DXGI_SAMPLE_DESC{1u, 0u};
                stream.sampleMask =
                    (std::numeric_limits<UINT>::max)();
                D3D12_PIPELINE_STATE_STREAM_DESC description{};
                description.SizeInBytes = sizeof(stream);
                description.pPipelineStateSubobjectStream = &stream;
                if (!HIKARI_DX_CHECK(
                        device2->CreatePipelineState(
                            &description,
                            IID_PPV_ARGS(drawPipelines_[shapeIndex]
                                [blendIndex][depthIndex].GetAddressOf())),
                        "ParticleGpuDrivenPass::CreateDrawPipeline")) {
                    return false;
                }
            }
          }
        }

        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
        D3D12_COMMAND_SIGNATURE_DESC signature{};
        signature.ByteStride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
        signature.NumArgumentDescs = 1u;
        signature.pArgumentDescs = &argument;
        return HIKARI_DX_CHECK(
            device->CreateCommandSignature(
                &signature,
                nullptr,
                IID_PPV_ARGS(
                    dispatchMeshCommandSignature_.GetAddressOf())),
            "ParticleGpuDrivenPass::CreateDispatchMeshSignature");
    }

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
