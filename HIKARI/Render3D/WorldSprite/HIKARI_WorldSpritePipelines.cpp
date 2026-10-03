#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpritePipelines.h"
#include <d3dx12.h>
#include <algorithm>
#include <limits>
#include <span>
#include <string>
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Assets/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"

namespace HIKARI::RENDER3D::WORLD_SPRITES {
using Microsoft::WRL::ComPtr;
namespace {
struct WorldSpritePipelineStateStream {
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
bool CreateRootSignature(ID3D12Device* device, ID3D12RootSignature** output) {
    if (device == nullptr || output == nullptr)
        return false;

    D3D12_DESCRIPTOR_RANGE textureRange{};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = GFX::DESCRIPTOR::kUserSrvCount;
    textureRange.BaseShaderRegister = 20u;

    std::array<D3D12_ROOT_PARAMETER, 15u> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0u;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[1].Descriptor.ShaderRegister = 1u;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1u;
    parameters[2].DescriptorTable.pDescriptorRanges = &textureRange;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[3].Constants.ShaderRegister = 1u;
    parameters[3].Constants.Num32BitValues = 5u;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[13].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[13].Constants.ShaderRegister = 3u;
    parameters[13].Constants.Num32BitValues = 16u;
    parameters[13].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    const UINT cbvRegisters[]{2, 4, 5};
    for (UINT index = 0; index < 3; ++index) {
        auto& cbv = parameters[4 + index];
        cbv.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        cbv.Descriptor.ShaderRegister = cbvRegisters[index];
        cbv.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        auto& srv = parameters[7 + index];
        srv.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        srv.Descriptor.ShaderRegister = index;
        srv.Descriptor.RegisterSpace = 5;
        srv.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    parameters[14].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[14].Descriptor.ShaderRegister = 2u;
    parameters[14].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    std::array<D3D12_DESCRIPTOR_RANGE, 3> lightingRanges{};
    const UINT textureRegisters[]{0, 9, 10};
    for (UINT index = 0; index < 3; ++index) {
        auto& range = lightingRanges[index];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = textureRegisters[index];
        range.RegisterSpace = index == 0 ? 9 : 0;
        auto& table = parameters[10 + index];
        table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        table.DescriptorTable = {1, &range};
        table.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0u;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    std::array<D3D12_STATIC_SAMPLER_DESC, 2> samplers{sampler, sampler};
    samplers[1].ShaderRegister = 2;
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    description.NumStaticSamplers = static_cast<UINT>(samplers.size());
    description.pStaticSamplers = samplers.data();
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(device, description, output),
                           "WorldSpritePass::CreateRootSignature");
}
bool ResolveShaders(ID3D12Device* device, bool meshPipeline, ASSETS::SHADER::ShaderAssetEntryLease& outMesh,
                    ASSETS::SHADER::ShaderAssetEntryLease& outPixel, const char* pixelEntry = "WorldSpritePS") {
    const GFX::ShaderDeviceCapabilities capabilities = GFX::QueryShaderDeviceCapabilities(device);
    const auto contract = ASSETS::SHADER::MakeRenderGeometryShaderContract(
        ASSETS::SHADER::RenderGeometryRootLayout::WorldSpriteDraw,
        {capabilities.shaderModel, capabilities.rootSignatureVersion, capabilities.featureMask});
    const auto resolve = [&contract, meshPipeline](const char* entry, ASSETS::SHADER::ShaderStage stage,
                                                   const char* profile,
                                                   ASSETS::SHADER::ShaderAssetEntryLease& outLease) {
        auto resolved =
            ASSETS::SHADER::ResolveBuiltinShaderEntry(meshPipeline ? "HIKARI.EngineShader.Render3D_WorldSpriteDraw"
                                                                   : "HIKARI.EngineShader.Render3D_WorldSpriteRaster",
                                                      {entry, stage, profile}, contract);
        std::string diagnostic{};
        const auto* artifact = resolved ? resolved.lease.GetArtifact() : nullptr;
        if (!resolved || artifact == nullptr ||
            !ASSETS::SHADER::ValidateRenderGeometryShaderArtifact(*artifact, diagnostic) ||
            resolved.lease.GetDxil().empty()) {
            if (diagnostic.empty())
                diagnostic = resolved.diagnostic;
            if (std::string_view(entry) != "WorldSpriteDepthOfFieldPS") {
                HIKARI_LOG_ERROR(std::string("[WorldSpritePass][Shader] ") + entry + ": " + diagnostic);
            }
            return false;
        }
        outLease = std::move(resolved.lease);
        return true;
    };
    return resolve(meshPipeline ? "WorldSpriteMS" : "WorldSpriteVS",
                   meshPipeline ? ASSETS::SHADER::ShaderStage::Mesh : ASSETS::SHADER::ShaderStage::Vertex,
                   meshPipeline ? "ms_6_5" : "vs_6_0", outMesh) &&
           resolve(pixelEntry, ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0", outPixel);
}

bool CreatePipelineState(ID3D12Device* device, ID3D12Device2* device2, bool meshPipeline,
                         ID3D12RootSignature* rootSignature, WorldSpritePipeline kind,
                         std::span<const uint8_t> geometryDxil, std::span<const uint8_t> pixelDxil,
                         ComPtr<ID3D12PipelineState>& pipeline) {
    if (device == nullptr || rootSignature == nullptr || geometryDxil.empty() ||
        (kind != WorldSpritePipeline::Depth && kind != WorldSpritePipeline::Shadow && pixelDxil.empty())) {
        return false;
    }
    const bool depthOnly = kind == WorldSpritePipeline::Depth || kind == WorldSpritePipeline::Shadow;
    const bool surface = kind == WorldSpritePipeline::Surface || kind == WorldSpritePipeline::SurfaceFeature;
    CD3DX12_BLEND_DESC blend(D3D12_DEFAULT);
    if (kind == WorldSpritePipeline::Blend) {
        auto& target = blend.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D12_BLEND_ONE;
        target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        target.BlendOp = D3D12_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    if (kind == WorldSpritePipeline::DepthOfFieldCapture) {
        blend.IndependentBlendEnable = TRUE;
        for (uint32_t targetIndex = 0u; targetIndex < 6u; ++targetIndex) {
            auto& target = blend.RenderTarget[targetIndex];
            target.BlendEnable = TRUE;
            target.SrcBlend = D3D12_BLEND_ONE;
            target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            target.BlendOp = D3D12_BLEND_OP_ADD;
            target.SrcBlendAlpha = D3D12_BLEND_ONE;
            target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            target.RenderTargetWriteMask = targetIndex < 3u ? D3D12_COLOR_WRITE_ENABLE_ALL
                                           : targetIndex < 5u
                                               ? D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN
                                               : D3D12_COLOR_WRITE_ENABLE_RED;
        }
    }
    CD3DX12_DEPTH_STENCIL_DESC depth(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = (kind == WorldSpritePipeline::Blend || kind == WorldSpritePipeline::DepthOfFieldCapture)
                               ? D3D12_DEPTH_WRITE_MASK_ZERO
                               : D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    D3D12_RT_FORMAT_ARRAY formats{};
    formats.NumRenderTargets = depthOnly                                          ? 0u
                               : kind == WorldSpritePipeline::DepthOfFieldCapture ? 6u
                               : kind == WorldSpritePipeline::SurfaceFeature      ? 3u
                                                                                  : 1u;
    if (!depthOnly) {
        formats.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    }
    if (kind == WorldSpritePipeline::DepthOfFieldCapture) {
        formats.RTFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        formats.RTFormats[2] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        formats.RTFormats[3] = DXGI_FORMAT_R32G32_FLOAT;
        formats.RTFormats[4] = DXGI_FORMAT_R32G32_FLOAT;
        formats.RTFormats[5] = DXGI_FORMAT_R32_FLOAT;
    }
    if (formats.NumRenderTargets == 3u) {
        formats.RTFormats[1] = DXGI_FORMAT_R32G32_UINT;
        formats.RTFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    if (meshPipeline) {
        if (device2 == nullptr) {
            return false;
        }
        WorldSpritePipelineStateStream stream{};
        stream.rootSignature = rootSignature;
        stream.meshShader = CD3DX12_SHADER_BYTECODE(geometryDxil.data(), geometryDxil.size());
        stream.pixelShader = CD3DX12_SHADER_BYTECODE(pixelDxil.data(), pixelDxil.size());
        stream.blend = blend;
        stream.depthStencil = depth;
        stream.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        stream.rasterizer = rasterizer;
        stream.rtvFormats = formats;
        stream.sampleDescription = DXGI_SAMPLE_DESC{1u, 0u};
        stream.sampleMask = (std::numeric_limits<UINT>::max)();
        D3D12_PIPELINE_STATE_STREAM_DESC description{};
        description.SizeInBytes = sizeof(stream);
        description.pPipelineStateSubobjectStream = &stream;
        return HIKARI_DX_CHECK(device2->CreatePipelineState(&description, IID_PPV_ARGS(pipeline.GetAddressOf())),
                               "WorldSpritePass::CreatePipeline");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = rootSignature;
    description.VS = {geometryDxil.data(), geometryDxil.size()};
    description.PS = {pixelDxil.data(), pixelDxil.size()};
    description.BlendState = blend;
    description.DepthStencilState = depth;
    description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    description.RasterizerState = rasterizer;
    description.NumRenderTargets = formats.NumRenderTargets;
    std::copy_n(formats.RTFormats, formats.NumRenderTargets, description.RTVFormats);
    description.SampleDesc = {1u, 0u};
    description.SampleMask = UINT_MAX;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    return HIKARI_DX_CHECK(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(pipeline.GetAddressOf())),
                           surface     ? "WorldSpritePass::CreateSurfacePipeline"
                           : depthOnly ? "WorldSpritePass::CreateDepthPipeline"
                                       : "WorldSpritePass::CreateRasterPipeline");
}
} // namespace
bool CreateWorldSpritePipelines(ID3D12Device* device, bool preferRaster, ComPtr<ID3D12RootSignature>& rootSignature,
                                WorldSpritePipelineSet& pipelines, bool& meshPipeline,
                                ASSETS::SHADER::ShaderAssetEntryLease& outGeometryShader) {
    if (!CreateRootSignature(device, rootSignature.GetAddressOf())) {
        return false;
    }
    ASSETS::SHADER::ShaderAssetEntryLease meshShader{};
    ASSETS::SHADER::ShaderAssetEntryLease pixelShader{};
    meshPipeline = !preferRaster && (GFX::QueryShaderDeviceCapabilities(device).featureMask &
                                     GFX::ShaderDeviceFeature::MeshShader) != 0u;
    if (!ResolveShaders(device, meshPipeline, meshShader, pixelShader)) {
        return false;
    }
    outGeometryShader = meshShader;
    ComPtr<ID3D12Device2> device2{};
    if (!HIKARI_DX_CHECK(device->QueryInterface(IID_PPV_ARGS(device2.GetAddressOf())),
                         "WorldSpritePass::QueryDevice2")) {
        return false;
    }
    std::array<ASSETS::SHADER::ShaderAssetEntryLease, 3> auxiliaryShaders;
    const char* auxiliaryEntries[]{"WorldSpriteDepthPS", "WorldSpriteSurfacePS", "WorldSpriteSurfaceFeaturePS"};
    for (uint32_t i = 0; i < auxiliaryShaders.size(); ++i) {
        ASSETS::SHADER::ShaderAssetEntryLease ignored;
        if (!ResolveShaders(device, meshPipeline, ignored, auxiliaryShaders[i], auxiliaryEntries[i]))
            return false;
    }
    ASSETS::SHADER::ShaderAssetEntryLease depthOfFieldShader{};
    ASSETS::SHADER::ShaderAssetEntryLease ignoredDepthOfFieldMesh{};
    const bool depthOfFieldShaderReady =
        ResolveShaders(device, meshPipeline, ignoredDepthOfFieldMesh, depthOfFieldShader, "WorldSpriteDepthOfFieldPS");
    for (uint32_t alphaMode = 0u; alphaMode < pipelines.size(); ++alphaMode) {
        const auto kind = static_cast<WorldSpritePipeline>(alphaMode);
        if (kind == WorldSpritePipeline::DepthOfFieldCapture && !depthOfFieldShaderReady) {
            continue;
        }
        const auto pixelDxil = kind == WorldSpritePipeline::DepthOfFieldCapture ? depthOfFieldShader.GetDxil()
                               : (kind == WorldSpritePipeline::Depth || kind == WorldSpritePipeline::Shadow)
                                   ? auxiliaryShaders[0].GetDxil()
                               : (kind == WorldSpritePipeline::Surface || kind == WorldSpritePipeline::SurfaceFeature)
                                   ? auxiliaryShaders[kind == WorldSpritePipeline::Surface ? 1u : 2u].GetDxil()
                                   : pixelShader.GetDxil();
        if (!CreatePipelineState(device, device2.Get(), meshPipeline, rootSignature.Get(), kind,
                                 outGeometryShader.GetDxil(), pixelDxil, pipelines[alphaMode])) {
            return false;
        }
    }
    return true;
}

bool CreateWorldSpriteProjectPipeline(ID3D12Device* device, bool meshPipeline, ID3D12RootSignature* rootSignature,
                                      WorldSpritePipeline kind, std::span<const uint8_t> geometryShaderDxil,
                                      std::span<const uint8_t> pixelShaderDxil, ComPtr<ID3D12PipelineState>& pipeline) {
    if (kind != WorldSpritePipeline::Opaque && kind != WorldSpritePipeline::Blend &&
        kind != WorldSpritePipeline::DepthOfFieldCapture) {
        return false;
    }
    ComPtr<ID3D12Device2> device2{};
    if (meshPipeline && !HIKARI_DX_CHECK(device->QueryInterface(IID_PPV_ARGS(device2.GetAddressOf())),
                                         "WorldSpritePass::QueryProjectDevice2")) {
        return false;
    }
    return CreatePipelineState(device, device2.Get(), meshPipeline, rootSignature, kind, geometryShaderDxil,
                               pixelShaderDxil, pipeline);
}
} // namespace HIKARI::RENDER3D::WORLD_SPRITES
