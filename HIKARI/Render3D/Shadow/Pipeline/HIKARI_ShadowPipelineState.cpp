#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <iterator>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "RenderContracts/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Render3D/Shadow/Pipeline/HIKARI_ShadowRootParameters.h"

namespace HIKARI::SHADOW::INTERNAL {

namespace {

D3D12_DESCRIPTOR_RANGE MakeSrvRange(UINT descriptorCount, UINT shaderRegister, UINT registerSpace = 0) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = descriptorCount;
    range.BaseShaderRegister = shaderRegister;
    range.RegisterSpace = registerSpace;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    return range;
}

D3D12_ROOT_PARAMETER MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE type, UINT shaderRegister, UINT registerSpace = 0,
                                        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = type;
    parameter.ShaderVisibility = visibility;
    parameter.Descriptor.ShaderRegister = shaderRegister;
    parameter.Descriptor.RegisterSpace = registerSpace;
    return parameter;
}

D3D12_ROOT_PARAMETER MakeRootDescriptorTable(const D3D12_DESCRIPTOR_RANGE& range,
                                             D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.ShaderVisibility = visibility;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    return parameter;
}

D3D12_ROOT_PARAMETER MakeRootConstants(UINT shaderRegister, UINT valueCount, UINT registerSpace = 0) {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameter.Constants.ShaderRegister = shaderRegister;
    parameter.Constants.RegisterSpace = registerSpace;
    parameter.Constants.Num32BitValues = valueCount;
    return parameter;
}

bool CreateNamedRootSignature(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& description,
                              ComPtr<ID3D12RootSignature>& rootSignature, const char* operation,
                              const wchar_t* debugName) {
    HRESULT createResult = GFX::CreateRootSignature(device, description, rootSignature.ReleaseAndGetAddressOf());
    if (!HIKARI_DX_CHECK(createResult, operation)) {
        return false;
    }
    GFX::SetD3D12Name(rootSignature.Get(), debugName);
    return true;
}

} // namespace

bool CreateShadowDepthPipelineState(ShadowRendererState& state, const GFX::Context& graphics) {
    ID3D12Device* device = graphics.device;
    const auto caps = GFX::QueryShaderDeviceCapabilities(device);
    if (caps.shaderModel < 60u) {
        DEBUGLOG::PushRenderError("[Shadow][ERROR] Shader Model 6.0 is not supported by this device.");
        return false;
    }

    using namespace ASSETS::SHADER;
    const auto contract = MakeRenderGeometryShaderContract(
        RenderGeometryRootLayout::ShadowGraphics, {caps.shaderModel, caps.rootSignatureVersion, caps.featureMask});
    const auto resolve = [&](const char* program, ShaderStage stage, const char* profile) {
        auto result = ResolveBuiltinRenderGeometryShaderEntry(program, {"Main", stage, profile}, contract);
        if (!result)
            DEBUGLOG::PushRenderError("[Shadow][Shader] " + std::string(program) + ": " + result.diagnostic);
        return result;
    };
    const auto staticVs = resolve("HIKARI.EngineShader.Render3D_ShadowStaticVS", ShaderStage::Vertex, "vs_6_0");
    const auto skinnedVs = resolve("HIKARI.EngineShader.Render3D_ShadowSkinnedVS", ShaderStage::Vertex, "vs_6_0");
    const auto ps = resolve("HIKARI.EngineShader.Render3D_ShadowAlphaPS", ShaderStage::Pixel, "ps_6_0");
    if (!staticVs || !skinnedVs || !ps)
        return false;
    const auto bytecode = [](const ShaderAssetResolveResult& shader) {
        const auto bytes = shader.lease.GetDxil();
        return D3D12_SHADER_BYTECODE{bytes.data(), bytes.size()};
    };

    const D3D12_DESCRIPTOR_RANGE textureRange = MakeSrvRange(1, 0);
    const D3D12_DESCRIPTOR_RANGE materialDataRange = MakeSrvRange(1, 16);
    const D3D12_DESCRIPTOR_RANGE surfaceGpuSceneRange = MakeSrvRange(1, 17);
    const D3D12_DESCRIPTOR_RANGE objectDataRange = MakeSrvRange(1, 15);
    const D3D12_DESCRIPTOR_RANGE texturePoolRange = MakeSrvRange(GFX::DESCRIPTOR::kUserSrvCount, 20);
    const D3D12_DESCRIPTOR_RANGE clusterGeometryPoolRange =
        MakeSrvRange(GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaCapacity, 0, 1);

    D3D12_ROOT_PARAMETER params[PIPELINE::kShadowStaticRootParamMaterialCamera + 1]{};
    params[PIPELINE::kShadowStaticRootParamCamera] = MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, 0);
    params[PIPELINE::kShadowStaticRootParamObject] = MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, 1);
    params[PIPELINE::kShadowStaticRootParamBaseColorTexture] =
        MakeRootDescriptorTable(textureRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[PIPELINE::kShadowStaticRootParamMaterialData] = MakeRootDescriptorTable(materialDataRange);
    params[PIPELINE::kShadowStaticRootParamSurfaceGpuScene] = MakeRootDescriptorTable(surfaceGpuSceneRange);
    params[PIPELINE::kShadowStaticRootParamSurfaceGpuSceneControl] =
        MakeRootConstants(8, RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount);
    params[PIPELINE::kShadowStaticRootParamTexturePool] =
        MakeRootDescriptorTable(texturePoolRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[PIPELINE::kShadowStaticRootParamMaterialIndex] = MakeRootConstants(7, 1);
    params[PIPELINE::kShadowStaticRootParamObjectData] = MakeRootDescriptorTable(objectDataRange);
    params[PIPELINE::kShadowStaticRootParamClusterGeometryPool] = MakeRootDescriptorTable(clusterGeometryPoolRange);
    params[PIPELINE::kShadowStaticRootParamMeshletVisibleRanges] =
        MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, 18);
    params[PIPELINE::kShadowStaticRootParamCullingCamera] = MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, 9);
    params[PIPELINE::kShadowStaticRootParamDeformationPalettes] =
        MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, 0, 3);
    params[PIPELINE::kShadowStaticRootParamTerrainSurfaceData] =
        MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, 4, 7);
    params[PIPELINE::kShadowStaticRootParamSurfaceFeatureFloats] =
        MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, 1, 7);
    params[PIPELINE::kShadowStaticRootParamMaterialProgramParameters] =
        MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, 0, 6);
    params[PIPELINE::kShadowStaticRootParamMaterialProgramResources] =
        MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, 1, 6);
    params[PIPELINE::kShadowStaticRootParamMaterialCamera] = MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, 10);
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(std::size(params));
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    if (!CreateNamedRootSignature(device, rsDesc, state.rootSig, "CreateRootSignature: Shadow static",
                                  L"Shadow Static RootSignature")) {
        return false;
    }

    D3D12_ROOT_PARAMETER skinnedParams[PIPELINE::kShadowSkinnedRootParamJointPalette + 1]{};
    for (size_t i = 0; i < std::size(params); ++i) {
        skinnedParams[i] = params[i];
    }
    skinnedParams[PIPELINE::kShadowSkinnedRootParamJointPalette] = MakeRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, 3);

    D3D12_ROOT_SIGNATURE_DESC skinnedRsDesc = rsDesc;
    skinnedRsDesc.NumParameters = static_cast<UINT>(std::size(skinnedParams));
    skinnedRsDesc.pParameters = skinnedParams;
    if (!CreateNamedRootSignature(device, skinnedRsDesc, state.skinnedRootSig, "CreateRootSignature: Shadow skinned",
                                  L"Shadow Skinned RootSignature")) {
        return false;
    }

    if (!HIKARI_DX_CHECK(state.depthPipelines.Initialize(device, state.rootSig.Get(), state.skinnedRootSig.Get(),
                                                         bytecode(staticVs), bytecode(skinnedVs), bytecode(ps)),
                         "Create PSO: Shadow depth pipelines")) {
        return false;
    }
    return true;
}

bool CreateShadowPipelineState(ShadowRendererState& state, const GFX::Context& graphics) {
    if (!CreateShadowDepthPipelineState(state, graphics))
        return false;
    ID3D12Device* device = graphics.device;
    if (!state.traditionalCommandStreamBuffer.Initialize(
            device, state.rootSig.Get(), PIPELINE::kShadowStaticRootParamSurfaceGpuSceneControl,
            RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount)) {
        DEBUGLOG::PushRenderError(
            "[ShadowMapRenderer][WARN] Shadow indirect draw buffer initialization failed. Direct shadow record path will be used.");
    } else if (!state.traditionalCommandStreamBuffer.InitializeSkinnedCommandStream(
                   device, state.skinnedRootSig.Get(), PIPELINE::kShadowStaticRootParamSurfaceGpuSceneControl,
                   PIPELINE::kShadowSkinnedRootParamJointPalette)) {
        DEBUGLOG::PushRenderError(
            "[ShadowMapRenderer][WARN] Shadow skinned indirect command signature initialization failed. Skinned GPU-driven shadow stream will be unavailable.");
    }
    if (!state.clusterGpuCullingPass.Initialize(graphics, state.rootSig.Get(),
                                                PIPELINE::kShadowStaticRootParamSurfaceGpuSceneControl,
                                                RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount)) {
        DEBUGLOG::PushRenderError(
            "[ShadowMapRenderer][WARN] Shadow cluster GPU culling initialization failed. GPU-driven shadow pass will be unavailable.");
    }
    if (!state.meshletRenderBackend.Initialize(device, state.rootSig.Get(),
                                               RENDER3D::MESHLET::MeshletPipelineMask::ShadowRenderer)) {
        DEBUGLOG::PushRenderError(
            "[ShadowMapRenderer][WARN] Shadow meshlet backend initialization failed. GPU-driven shadow mesh shader route will be unavailable.");
    }
    state.clusterGpuDrivenProducer.Attach(&state.clusterGpuCullingPass);
    state.gpuDrivenLayer.Attach(&state.surfaceGpuSceneBuffer, nullptr, &state.traditionalCommandStreamBuffer,
                                &state.clusterGpuDrivenProducer, nullptr, &state.terrainSurfaceGpuFrameBuffer);
    if (!state.gpuDrivenLayer.Initialize(device, state.rootSig.Get(),
                                         PIPELINE::kShadowStaticRootParamSurfaceGpuSceneControl,
                                         RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount)) {
        DEBUGLOG::PushRenderError(
            "[ShadowMapRenderer][WARN] Shadow GPU-driven layer initialization failed. Shadow draw backend will be unavailable.");
    }
    return true;
}

bool EnsureShadowRendererInitialized(ShadowRendererState& state, const GFX::Context& graphics) {
    if (state.initialized) {
        return true;
    }
    auto* device = graphics.device;
    if (device == nullptr) {
        return false;
    }
    state.fallbackTextureResource = RENDER3D::CreateSolidColorTextureResource(
        "shadow/fallback_white", 0xffffffffu, RENDER3D::TextureResourceColorSpace::Linear);
    state.fallbackTextureHandle = RENDER3D::GetTextureResourceBackendHandle(state.fallbackTextureResource);
    if (!RENDER3D::IsTextureResourceValid(state.fallbackTextureResource)) {
        DEBUGLOG::PushRenderError("[Shadow][ERROR] Failed to create generated fallback white texture.");
        return false;
    }
    state.initialized = CreateShadowFrameResources(state, graphics) && CreateShadowPipelineState(state, graphics);
    return state.initialized;
}

} // namespace HIKARI::SHADOW::INTERNAL
