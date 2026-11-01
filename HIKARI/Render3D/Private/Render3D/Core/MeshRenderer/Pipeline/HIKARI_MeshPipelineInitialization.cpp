#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Core/HIKARI_MathConfig.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"

#include <algorithm>
#include <iterator>
#include <span>
#include <string>
#include <utility>

#include <d3dx12.h>

#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "RenderContracts/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshInputLayouts.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"

namespace HIKARI::MESHRENDERER {

using Microsoft::WRL::ComPtr;

bool InitializeMeshPipelines(ID3D12Device* device, MeshPipelineStore& store) {
    const GFX::ShaderDeviceCapabilities capabilities = GFX::QueryShaderDeviceCapabilities(device);
    if (capabilities.shaderModel < 60u) {
        DEBUGLOG::PushRenderError("[MeshRenderer][ERROR] Shader Model 6.0 is not supported by this device.");
        return false;
    }

    const ASSETS::SHADER::ShaderAssetContractExpectation contract = ASSETS::SHADER::MakeRenderGeometryShaderContract(
        ASSETS::SHADER::RenderGeometryRootLayout::MeshGraphics,
        {capabilities.shaderModel, capabilities.rootSignatureVersion, capabilities.featureMask});
    const auto resolveShader = [&contract](const char* programId, ASSETS::SHADER::ShaderStage stage,
                                           const char* profile, ASSETS::SHADER::ShaderAssetEntryLease& outShader) {
        ASSETS::SHADER::ShaderAssetResolveResult resolved =
            ASSETS::SHADER::ResolveBuiltinRenderGeometryShaderEntry(programId, {"Main", stage, profile}, contract);
        if (!resolved) {
            DEBUGLOG::PushRenderError(std::string("[MeshRenderer][Shader][ERROR] ") + programId + ": " +
                                      resolved.diagnostic);
            return false;
        }
        outShader = std::move(resolved.lease);
        return true;
    };

    if (!resolveShader("HIKARI.EngineShader.Render3D_StaticVS", ASSETS::SHADER::ShaderStage::Vertex, "vs_6_0",
                       store.vsShader) ||
        !resolveShader("HIKARI.EngineShader.Render3D_SkinnedVS", ASSETS::SHADER::ShaderStage::Vertex, "vs_6_0",
                       store.skinnedVsShader) ||
        !resolveShader("HIKARI.EngineShader.Render3D_StaticPS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                       store.psShader)) {
        return false;
    }
    if (!resolveShader("HIKARI.EngineShader.Render3D_StaticFxPS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                       store.materialFxPsShader)) {
        DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Cooked MaterialFx shader is "
                                  "unavailable; affected variants use the base pipeline.");
    }
    const bool waterDepthShaderReady =
        resolveShader("HIKARI.EngineShader.Render3D_WaterVisibilityPS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                      store.waterDepthPsShader);
    if (!waterDepthShaderReady) {
        DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Water visibility depth shader is "
                                  "unavailable; volumetric lighting will use opaque SceneDepth.");
    }
    if (!resolveShader("HIKARI.EngineShader.Render3D_GeometryAuxPS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                       store.geometryPsShader)) {
        return false;
    }
    const bool geometryFeatureMaskShaderReady =
        resolveShader("HIKARI.EngineShader.Render3D_GeometryAuxFeaturePS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                      store.geometryFeatureMaskPsShader);
    const bool geometryOutlineShadersReady =
        resolveShader("HIKARI.EngineShader.Render3D_GeometryOutlineVS", ASSETS::SHADER::ShaderStage::Vertex, "vs_6_0",
                      store.geometryOutlineVsShader) &&
        resolveShader("HIKARI.EngineShader.Render3D_SkinnedGeometryOutlineVS", ASSETS::SHADER::ShaderStage::Vertex,
                      "vs_6_0", store.geometryOutlineSkinnedVsShader) &&
        resolveShader("HIKARI.EngineShader.Render3D_GeometryOutlinePS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                      store.geometryOutlinePsShader);
    const bool visibilityOverlayShaderReady =
        resolveShader("HIKARI.EngineShader.Render3D_VisibilityOverlayPS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0",
                      store.visibilityOverlayPsShader);
    if (!geometryFeatureMaskShaderReady || !geometryOutlineShadersReady || !visibilityOverlayShaderReady) {
        DEBUGLOG::PushRenderError("[MeshRenderer][WARN] One or more optional stylized feature "
                                  "shaders failed to compile. Core Forward rendering remains "
                                  "available and the affected feature will be skipped.");
    }
    const auto shaderBytecode = [](const ASSETS::SHADER::ShaderAssetEntryLease& shader) noexcept {
        const std::span<const uint8_t> bytes = shader.GetDxil();
        return D3D12_SHADER_BYTECODE{bytes.data(), bytes.size()};
    };

    const auto srvRange = [](UINT count, UINT baseRegister, UINT space) {
        CD3DX12_DESCRIPTOR_RANGE range{};
        range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, count, baseRegister, space);
        return range;
    };
    const CD3DX12_DESCRIPTOR_RANGE shadowTextureRange = srvRange(1, 2, 0);
    const CD3DX12_DESCRIPTOR_RANGE skyCubeTextureRange = srvRange(1, 6, 0);
    const CD3DX12_DESCRIPTOR_RANGE sceneDepthTextureRange = srvRange(1, 7, 0);
    const CD3DX12_DESCRIPTOR_RANGE sceneColorTextureRange = srvRange(1, 8, 0);
    const CD3DX12_DESCRIPTOR_RANGE iblIrradianceRange = srvRange(1, 9, 0);
    const CD3DX12_DESCRIPTOR_RANGE iblPrefilteredRange = srvRange(1, 10, 0);
    const CD3DX12_DESCRIPTOR_RANGE iblBrdfLutRange = srvRange(1, 11, 0);
    const CD3DX12_DESCRIPTOR_RANGE reflectionProbePrefilteredRange = srvRange(1, 12, 0);
    const CD3DX12_DESCRIPTOR_RANGE reflectionProbePrefilteredSecondaryRange = srvRange(1, 14, 0);
    const CD3DX12_DESCRIPTOR_RANGE ssaoRange = srvRange(1, 13, 0);
    // Light probe volume: SH 係数 Texture3D ×9 (t0-t8, space2)。
    // heap 側は SystemSrv::LightProbeShVolume0..8 の連続スロットに対応する。
    const CD3DX12_DESCRIPTOR_RANGE lightProbeShRange = srvRange(GFX::DESCRIPTOR::kLightProbeShVolumeTextureCount, 0, 2);
    const CD3DX12_DESCRIPTOR_RANGE lightProbeShSecondaryRange =
        srvRange(GFX::DESCRIPTOR::kLightProbeShVolumeTextureCount, 0, 4);
    const CD3DX12_DESCRIPTOR_RANGE objectDataRange = srvRange(1, 15, 0);
    const CD3DX12_DESCRIPTOR_RANGE materialDataRange = srvRange(1, 16, 0);
    CD3DX12_DESCRIPTOR_RANGE surfaceGpuSceneRanges[2]{};
    surfaceGpuSceneRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 17, 0, 0u);
    surfaceGpuSceneRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 8, 1u);
    const CD3DX12_DESCRIPTOR_RANGE materialTexturePoolRange = srvRange(GFX::DESCRIPTOR::kUserSrvCount, 20, 0);
    const CD3DX12_DESCRIPTOR_RANGE clusterGeometryPoolRange =
        srvRange(GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaCapacity, 0, 1);
    const CD3DX12_DESCRIPTOR_RANGE screenSpaceDepthPyramidRange = srvRange(1, 2, 7);
    const CD3DX12_DESCRIPTOR_RANGE screenSpacePackedSurfaceRange = srvRange(1, 3, 7);
    const CD3DX12_DESCRIPTOR_RANGE oceanSpectrumRange = srvRange(6u, 5u, 7u);
    const CD3DX12_DESCRIPTOR_RANGE rayTracedShadowRange = srvRange(1u, 11u, 7u);
    CD3DX12_DESCRIPTOR_RANGE meshletOcclusionRanges[3]{};
    meshletOcclusionRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 19u, 0u, 0u);
    meshletOcclusionRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 0u, 8u, 1u);
    meshletOcclusionRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1u, 12u, 0u, 2u);

    constexpr D3D12_SHADER_VISIBILITY kAll = D3D12_SHADER_VISIBILITY_ALL;
    constexpr D3D12_SHADER_VISIBILITY kPixel = D3D12_SHADER_VISIBILITY_PIXEL;
    // 既存 slot を動かさず、GPU-driven 用のリソースプールを末尾へ追加する。
    CD3DX12_ROOT_PARAMETER params[ROOT_PARAM::Count]{};
    params[ROOT_PARAM::Camera].InitAsConstantBufferView(0, 0, kAll);
    params[ROOT_PARAM::CullingCamera].InitAsConstantBufferView(9, 0, kAll);
    params[ROOT_PARAM::Object].InitAsConstantBufferView(1, 0, kAll);
    params[ROOT_PARAM::Light].InitAsConstantBufferView(2, 0, kPixel);
    params[ROOT_PARAM::ShadowMap].InitAsDescriptorTable(1, &shadowTextureRange, kPixel);
    params[ROOT_PARAM::ShadowCB].InitAsConstantBufferView(4, 0, kPixel);
    params[ROOT_PARAM::SkyEnvironment].InitAsConstantBufferView(5, 0, kPixel);
    params[ROOT_PARAM::SkyCube].InitAsDescriptorTable(1, &skyCubeTextureRange, kPixel);
    params[ROOT_PARAM::SceneDepth].InitAsDescriptorTable(1, &sceneDepthTextureRange, kPixel);
    params[ROOT_PARAM::SceneColor].InitAsDescriptorTable(1, &sceneColorTextureRange, kPixel);
    params[ROOT_PARAM::IblIrradiance].InitAsDescriptorTable(1, &iblIrradianceRange, kPixel);
    params[ROOT_PARAM::IblPrefiltered].InitAsDescriptorTable(1, &iblPrefilteredRange, kPixel);
    params[ROOT_PARAM::IblBrdfLut].InitAsDescriptorTable(1, &iblBrdfLutRange, kPixel);
    params[ROOT_PARAM::ReflectionProbePrefiltered].InitAsDescriptorTable(1, &reflectionProbePrefilteredRange, kPixel);
    params[ROOT_PARAM::Ssao].InitAsDescriptorTable(1, &ssaoRange, kPixel);
    params[ROOT_PARAM::LightProbeSh].InitAsDescriptorTable(1, &lightProbeShRange, kPixel);
    params[ROOT_PARAM::ReflectionProbePrefilteredSecondary].InitAsDescriptorTable(
        1, &reflectionProbePrefilteredSecondaryRange, kPixel);
    params[ROOT_PARAM::LightProbeShSecondary].InitAsDescriptorTable(1, &lightProbeShSecondaryRange, kPixel);
    params[ROOT_PARAM::ObjectData].InitAsDescriptorTable(1, &objectDataRange, kAll);
    params[ROOT_PARAM::ObjectIndex].InitAsConstants(1, 6, 0, kAll);
    params[ROOT_PARAM::MaterialData].InitAsDescriptorTable(1, &materialDataRange, kAll);
    params[ROOT_PARAM::MaterialIndex].InitAsConstants(1, 7, 0, kAll);
    params[ROOT_PARAM::TexturePool].InitAsDescriptorTable(1, &materialTexturePoolRange, kPixel);
    params[ROOT_PARAM::JointPalette].InitAsShaderResourceView(0, 3, kAll);
    params[ROOT_PARAM::SurfaceGpuScene].InitAsDescriptorTable(static_cast<UINT>(std::size(surfaceGpuSceneRanges)),
                                                              surfaceGpuSceneRanges, kAll);
    params[ROOT_PARAM::SurfaceGpuSceneControl].InitAsConstants(4, 8, 0, kAll);
    params[ROOT_PARAM::ClusterGeometryPool].InitAsDescriptorTable(1, &clusterGeometryPoolRange, kAll);
    params[ROOT_PARAM::MeshletVisibleRanges].InitAsShaderResourceView(18, 0, kAll);
    params[ROOT_PARAM::MeshletOcclusionDepth].InitAsDescriptorTable(3u, meshletOcclusionRanges,
                                                                    D3D12_SHADER_VISIBILITY_AMPLIFICATION);
    params[ROOT_PARAM::LocalLights].InitAsShaderResourceView(0, 5, kPixel);
    params[ROOT_PARAM::LocalLightClusterHeaders].InitAsShaderResourceView(1, 5, kPixel);
    params[ROOT_PARAM::LocalLightClusterIndices].InitAsShaderResourceView(2, 5, kPixel);
    params[ROOT_PARAM::MaterialProgramParameters].InitAsShaderResourceView(0, 6, kAll);
    params[ROOT_PARAM::MaterialProgramResources].InitAsShaderResourceView(1, 6, kAll);
    params[ROOT_PARAM::SurfaceFeatureData].InitAsShaderResourceView(0, 7, kAll);
    params[ROOT_PARAM::TerrainSurfaceData].InitAsShaderResourceView(4, 7, kAll);
    params[ROOT_PARAM::SurfaceFeatureFloats].InitAsShaderResourceView(1, 7, kAll);
    params[ROOT_PARAM::ScreenSpaceDepthPyramid].InitAsDescriptorTable(1, &screenSpaceDepthPyramidRange, kPixel);
    params[ROOT_PARAM::ScreenSpacePackedSurface].InitAsDescriptorTable(1, &screenSpacePackedSurfaceRange, kPixel);
    params[ROOT_PARAM::ScreenSpaceTraceControl].InitAsConstants(2, 10, 0, kPixel);
    params[ROOT_PARAM::OceanSpectrumData].InitAsDescriptorTable(1u, &oceanSpectrumRange, kAll);
    params[ROOT_PARAM::OceanSpectrumControl].InitAsConstants(2u, 11u, 0u, kAll);
    params[ROOT_PARAM::RayTracedShadow].InitAsDescriptorTable(1u, &rayTracedShadowRange, kPixel);

    // 零初期化した上で必要な欄だけを設定する（比較関数・境界色の既定値を持ち込まない）。
    const auto sampler = [](UINT shaderRegister, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE addressU,
                            D3D12_TEXTURE_ADDRESS_MODE addressVW) {
        D3D12_STATIC_SAMPLER_DESC desc{};
        desc.Filter = filter;
        desc.AddressU = addressU;
        desc.AddressV = addressVW;
        desc.AddressW = addressVW;
        desc.ShaderRegister = shaderRegister;
        desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        desc.MaxLOD = D3D12_FLOAT32_MAX;
        return desc;
    };
    D3D12_STATIC_SAMPLER_DESC linearWrapSampler =
        sampler(0, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    linearWrapSampler.MaxAnisotropy = 8;
    D3D12_STATIC_SAMPLER_DESC shadowSampler =
        sampler(1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                D3D12_TEXTURE_ADDRESS_MODE_BORDER);
    shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    const D3D12_STATIC_SAMPLER_DESC staticSamplers[] = {
        linearWrapSampler,
        shadowSampler,
        sampler(2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        sampler(3, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        sampler(4, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
    };

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(std::size(params));
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = static_cast<UINT>(std::size(staticSamplers));
    rsDesc.pStaticSamplers = staticSamplers;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (FAILED(GFX::CreateRootSignature(device, rsDesc, store.rootSig.GetAddressOf()))) {
        return false;
    }

    // Skinned VS/PS は同じ slot を b3 CBV として上書きする。
    CD3DX12_ROOT_PARAMETER skinnedParams[ROOT_PARAM::Count]{};
    std::copy(std::begin(params), std::end(params), skinnedParams);
    skinnedParams[ROOT_PARAM::JointPalette].InitAsConstantBufferView(3, 0, kAll);
    D3D12_ROOT_SIGNATURE_DESC skinnedRsDesc = rsDesc;
    skinnedRsDesc.pParameters = skinnedParams;
    if (FAILED(GFX::CreateRootSignature(device, skinnedRsDesc, store.skinnedRootSig.GetAddressOf()))) {
        return false;
    }

    const auto inputElements = GetStaticMeshInputLayout();
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = store.rootSig.Get();
    psoDesc.VS = shaderBytecode(store.vsShader);
    psoDesc.PS = shaderBytecode(store.psShader);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.FrontCounterClockwise = MATH::kFrontFaceCounterClockwise ? TRUE : FALSE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    // 通常描画は既存 variant cache が必要な PSO を作る。未使用の
    // traditional fallback を Mesh Shader 起動時に先行コンパイルしない。
    store.fallbackDesc = psoDesc;

    const auto skinnedInputElements = GetSkinnedMeshInputLayout();
    const auto skinned = [&](D3D12_GRAPHICS_PIPELINE_STATE_DESC desc,
                             const ASSETS::SHADER::ShaderAssetEntryLease& vertexShader) {
        desc.pRootSignature = store.skinnedRootSig.Get();
        desc.VS = shaderBytecode(vertexShader);
        desc.InputLayout = {skinnedInputElements.data(), static_cast<UINT>(skinnedInputElements.size())};
        return desc;
    };
    const auto create = [device](const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                 ComPtr<ID3D12PipelineState>& outPipeline) {
        return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(outPipeline.GetAddressOf())));
    };
    // 任意 feature の静的/skinned 対。どちらかが失敗したら両方を無効にして警告する。
    const auto createOptionalPair = [&](const D3D12_GRAPHICS_PIPELINE_STATE_DESC& staticDesc,
                                        const ASSETS::SHADER::ShaderAssetEntryLease& skinnedVertexShader,
                                        ComPtr<ID3D12PipelineState>& staticPipeline,
                                        ComPtr<ID3D12PipelineState>& skinnedPipeline, const char* warning) {
        if (!create(staticDesc, staticPipeline) || !create(skinned(staticDesc, skinnedVertexShader), skinnedPipeline)) {
            staticPipeline.Reset();
            skinnedPipeline.Reset();
            DEBUGLOG::PushRenderError(warning);
        }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC depthPsoDesc = psoDesc;
    depthPsoDesc.PS = {};
    depthPsoDesc.NumRenderTargets = 0;
    for (DXGI_FORMAT& format : depthPsoDesc.RTVFormats) {
        format = DXGI_FORMAT_UNKNOWN;
    }
    if (!create(depthPsoDesc, store.depthPso) ||
        !create(skinned(depthPsoDesc, store.skinnedVsShader), store.depthSkinnedPso)) {
        return false;
    }

    if (waterDepthShaderReady) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC waterDepthPsoDesc = depthPsoDesc;
        waterDepthPsoDesc.PS = shaderBytecode(store.waterDepthPsShader);
        if (!create(waterDepthPsoDesc, store.waterDepthPso)) {
            store.waterDepthPso.Reset();
        }
        if (!create(skinned(waterDepthPsoDesc, store.skinnedVsShader), store.waterDepthSkinnedPso)) {
            store.waterDepthSkinnedPso.Reset();
        }
        if (store.waterDepthPso == nullptr || store.waterDepthSkinnedPso == nullptr) {
            DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Water visibility depth PSO is "
                                      "unavailable; volumetric lighting will use opaque SceneDepth.");
        }
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geometryPsoDesc = psoDesc;
    geometryPsoDesc.PS = shaderBytecode(store.geometryPsShader);
    if (!create(geometryPsoDesc, store.geometryPso) ||
        !create(skinned(geometryPsoDesc, store.skinnedVsShader), store.geometrySkinnedPso)) {
        return false;
    }

    if (geometryFeatureMaskShaderReady) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC featureMaskPsoDesc = geometryPsoDesc;
        featureMaskPsoDesc.PS = shaderBytecode(store.geometryFeatureMaskPsShader);
        featureMaskPsoDesc.NumRenderTargets = 3u;
        featureMaskPsoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32_UINT;
        featureMaskPsoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
        createOptionalPair(featureMaskPsoDesc, store.skinnedVsShader, store.geometryFeatureMaskPso,
                           store.geometryFeatureMaskSkinnedPso,
                           "[MeshRenderer][WARN] Optional stylized feature-mask pipeline is unavailable. "
                           "Core Forward rendering remains available.");
    }

    if (geometryOutlineShadersReady) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC outlinePsoDesc = psoDesc;
        outlinePsoDesc.VS = shaderBytecode(store.geometryOutlineVsShader);
        outlinePsoDesc.PS = shaderBytecode(store.geometryOutlinePsShader);
        outlinePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
        outlinePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        // outline は D3D12 info queue も診断へ出す。
        const auto createDiagnosed = [&](const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                         ComPtr<ID3D12PipelineState>& outPipeline, const char* label) {
            GFX::ClearD3D12InfoQueue(device);
            const HRESULT result = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(outPipeline.GetAddressOf()));
            if (FAILED(result)) {
                HIKARI_DX_CHECK(result, label);
                GFX::DumpD3D12InfoQueue(device, label);
            }
            return SUCCEEDED(result);
        };
        if (!createDiagnosed(outlinePsoDesc, store.geometryOutlinePso,
                             "MeshRenderer::CreateTraditionalGeometryOutlineStaticPSO") ||
            !createDiagnosed(skinned(outlinePsoDesc, store.geometryOutlineSkinnedVsShader),
                             store.geometryOutlineSkinnedPso,
                             "MeshRenderer::CreateTraditionalGeometryOutlineSkinnedPSO")) {
            store.geometryOutlinePso.Reset();
            store.geometryOutlineSkinnedPso.Reset();
            DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Optional traditional VS/PS "
                                      "geometry-outline pipeline is unavailable. Mesh Shader "
                                      "outline readiness is evaluated independently.");
        }
    }

    if (visibilityOverlayShaderReady) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC visibilityPsoDesc = psoDesc;
        visibilityPsoDesc.PS = shaderBytecode(store.visibilityOverlayPsShader);
        ApplyCompositeBlendState(visibilityPsoDesc.BlendState, VFX::CompositeMode::Alpha);
        visibilityPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        visibilityPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
        createOptionalPair(visibilityPsoDesc, store.skinnedVsShader, store.visibilityOverlayPso,
                           store.visibilityOverlaySkinnedPso,
                           "[MeshRenderer][WARN] Optional visibility-overlay pipeline is unavailable. "
                           "Core Forward rendering remains available.");
    }

    return true;
}

} // namespace HIKARI::MESHRENDERER
