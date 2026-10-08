#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"

namespace HIKARI::MESHRENDERER {

namespace {
void ResetRootParamCache(MeshBindingStateCache& cache) {
    std::fill(cache.cbvAddresses.begin(), cache.cbvAddresses.end(), 0);
    for (D3D12_GPU_DESCRIPTOR_HANDLE& handle : cache.descriptorTables) {
        handle.ptr = 0;
    }
    std::fill(cache.rootConstants.begin(), cache.rootConstants.end(), 0u);
    std::fill(cache.rootConstantValid.begin(), cache.rootConstantValid.end(), false);
}

void BindRootSignatureCached(const MeshBindingContext& ctx, ID3D12RootSignature* rootSig) {
    if (ctx.cmd == nullptr || rootSig == nullptr) {
        return;
    }

    MeshBindingStateCache* cache = ctx.cache;
    if (cache != nullptr && cache->rootSignature == rootSig) {
        return;
    }

    ctx.cmd->SetGraphicsRootSignature(rootSig);
    if (cache != nullptr) {
        cache->rootSignature = rootSig;
        // root signature 変更時は root 引数を再設定する。
        ResetRootParamCache(*cache);
    }
}

void BindCbvCached(const MeshBindingContext& ctx, UINT rootParam, D3D12_GPU_VIRTUAL_ADDRESS address) {
    if (ctx.cmd == nullptr || rootParam >= kTrackedRootParamCount) {
        return;
    }

    MeshBindingStateCache* cache = ctx.cache;
    if (cache != nullptr && cache->rootSignature == nullptr) {
        return;
    }
    if (cache != nullptr && cache->cbvAddresses[rootParam] == address) {
        return;
    }

    ctx.cmd->SetGraphicsRootConstantBufferView(rootParam, address);
    if (cache != nullptr) {
        cache->cbvAddresses[rootParam] = address;
    }
}

void BindSrvCached(const MeshBindingContext& ctx, UINT rootParam, D3D12_GPU_VIRTUAL_ADDRESS address) {
    if (ctx.cmd == nullptr || rootParam >= kTrackedRootParamCount || address == 0u) {
        return;
    }
    MeshBindingStateCache* cache = ctx.cache;
    if (cache != nullptr && cache->rootSignature == nullptr) {
        return;
    }
    if (cache != nullptr && cache->cbvAddresses[rootParam] == address) {
        return;
    }
    ctx.cmd->SetGraphicsRootShaderResourceView(rootParam, address);
    if (cache != nullptr) {
        cache->cbvAddresses[rootParam] = address;
    }
}

bool BindDescriptorTableCached(const MeshBindingContext& ctx, UINT rootParam, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    if (ctx.cmd == nullptr || rootParam >= kTrackedRootParamCount || handle.ptr == 0) {
        return false;
    }

    MeshBindingStateCache* cache = ctx.cache;
    if (cache != nullptr && cache->rootSignature == nullptr) {
        return false;
    }
    if (cache != nullptr && cache->descriptorTables[rootParam].ptr == handle.ptr) {
        return false;
    }

    ctx.cmd->SetGraphicsRootDescriptorTable(rootParam, handle);
    if (cache != nullptr) {
        cache->descriptorTables[rootParam] = handle;
    }
    return true;
}

void BindObjectDataBufferCached(const MeshBindingContext& ctx, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    if (ctx.cmd == nullptr || handle.ptr == 0) {
        return;
    }

    MeshBindingStateCache* cache = ctx.cache;
    if (cache != nullptr && cache->rootSignature == nullptr) {
        return;
    }
    if (cache != nullptr && cache->descriptorTables[ROOT_PARAM::ObjectData].ptr == handle.ptr) {
        return;
    }

    ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::ObjectData, handle);
    if (cache != nullptr) {
        cache->descriptorTables[ROOT_PARAM::ObjectData] = handle;
    }
}

void BindMaterialDataBufferCached(const MeshBindingContext& ctx, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    if (ctx.cmd == nullptr || handle.ptr == 0) {
        return;
    }

    MeshBindingStateCache* cache = ctx.cache;
    if (cache != nullptr && cache->rootSignature == nullptr) {
        return;
    }
    if (cache != nullptr && cache->descriptorTables[ROOT_PARAM::MaterialData].ptr == handle.ptr) {
        return;
    }

    ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::MaterialData, handle);
    if (cache != nullptr) {
        cache->descriptorTables[ROOT_PARAM::MaterialData] = handle;
    }
}

void BindSurfaceGpuSceneBufferCached(const MeshBindingContext& ctx, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    if (ctx.cmd == nullptr || handle.ptr == 0 || ROOT_PARAM::SurfaceGpuScene >= kTrackedRootParamCount) {
        return;
    }

    MeshBindingStateCache* cache = ctx.cache;
    if (cache != nullptr && cache->rootSignature == nullptr) {
        return;
    }
    if (cache != nullptr && cache->descriptorTables[ROOT_PARAM::SurfaceGpuScene].ptr == handle.ptr) {
        return;
    }

    ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::SurfaceGpuScene, handle);
    if (cache != nullptr) {
        cache->descriptorTables[ROOT_PARAM::SurfaceGpuScene] = handle;
    }
}

void BindRootConstantCached(const MeshBindingContext& ctx, UINT rootParam, uint32_t value) {
    if (ctx.cmd == nullptr || rootParam >= kTrackedRootParamCount) {
        return;
    }

    MeshBindingStateCache* cache = ctx.cache;
    if (cache != nullptr && cache->rootSignature == nullptr) {
        return;
    }
    if (cache != nullptr && cache->rootConstantValid[rootParam] && cache->rootConstants[rootParam] == value) {
        return;
    }

    ctx.cmd->SetGraphicsRoot32BitConstant(rootParam, value, 0);
    if (cache != nullptr) {
        cache->rootConstants[rootParam] = value;
        cache->rootConstantValid[rootParam] = true;
    }
}

} // namespace
// フレーム全体で共通のリソースをバインドする。これには、カメラ、ライト、シャドウ、スカイ環境の定数バッファが含まれる。ルートシグネチャも設定される。
void BindFrameCommonResources(const MeshBindingContext& ctx, ID3D12RootSignature* rootSig,
                              D3D12_GPU_VIRTUAL_ADDRESS cameraAddress, D3D12_GPU_VIRTUAL_ADDRESS cullingCameraAddress,
                              D3D12_GPU_VIRTUAL_ADDRESS lightAddress, D3D12_GPU_VIRTUAL_ADDRESS localLightsAddress,
                              D3D12_GPU_VIRTUAL_ADDRESS localLightClusterHeadersAddress,
                              D3D12_GPU_VIRTUAL_ADDRESS localLightClusterIndicesAddress,
                              D3D12_GPU_VIRTUAL_ADDRESS materialProgramParametersAddress,
                              D3D12_GPU_VIRTUAL_ADDRESS materialProgramResourcesAddress,
                              D3D12_GPU_VIRTUAL_ADDRESS shadowAddress,
                              D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress) {
    if (ctx.cmd == nullptr || rootSig == nullptr) {
        return;
    }

    BindRootSignatureCached(ctx, rootSig);
    BindCbvCached(ctx, ROOT_PARAM::Camera, cameraAddress);
    BindCbvCached(ctx, ROOT_PARAM::CullingCamera, cullingCameraAddress);
    BindCbvCached(ctx, ROOT_PARAM::Light, lightAddress);
    BindSrvCached(ctx, ROOT_PARAM::LocalLights, localLightsAddress);
    BindSrvCached(ctx, ROOT_PARAM::LocalLightClusterHeaders, localLightClusterHeadersAddress);
    BindSrvCached(ctx, ROOT_PARAM::LocalLightClusterIndices, localLightClusterIndicesAddress);
    BindSrvCached(ctx, ROOT_PARAM::MaterialProgramParameters, materialProgramParametersAddress);
    BindSrvCached(ctx, ROOT_PARAM::MaterialProgramResources, materialProgramResourcesAddress);
    BindCbvCached(ctx, ROOT_PARAM::ShadowCB, shadowAddress);
    BindCbvCached(ctx, ROOT_PARAM::SkyEnvironment, skyEnvironmentAddress);
    BindMaterialTexturePool(ctx);
    BindClusterGeometryPool(ctx);
}

void BindObjectDataBuffer(const MeshBindingContext& ctx, D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrv) {
    BindObjectDataBufferCached(ctx, objectDataSrv);
}

void BindObjectDataIndex(const MeshBindingContext& ctx, uint32_t objectIndex) {
    BindRootConstantCached(ctx, ROOT_PARAM::ObjectIndex, objectIndex);
}

void BindMaterialDataBuffer(const MeshBindingContext& ctx, D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrv) {
    BindMaterialDataBufferCached(ctx, materialDataSrv);
}

void BindMaterialDataIndex(const MeshBindingContext& ctx, uint32_t materialIndex) {
    BindRootConstantCached(ctx, ROOT_PARAM::MaterialIndex, materialIndex);
}

void BindSurfaceGpuSceneBuffer(const MeshBindingContext& ctx, D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrv) {
    BindSurfaceGpuSceneBufferCached(ctx, surfaceGpuSceneSrv);
}

void BindSurfaceGpuSceneControl(const MeshBindingContext& ctx, uint32_t baseInstanceIndex, bool enabled) {
    if (ctx.cmd == nullptr || ROOT_PARAM::SurfaceGpuSceneControl >= kTrackedRootParamCount) {
        return;
    }
    if (ctx.cache != nullptr && ctx.cache->rootSignature == nullptr) {
        return;
    }

    const uint32_t constants[4] = {baseInstanceIndex, enabled ? 1u : 0u, 0u, 0u};
    ctx.cmd->SetGraphicsRoot32BitConstants(ROOT_PARAM::SurfaceGpuSceneControl, 4, constants, 0);
    if (ctx.cache != nullptr) {
        ctx.cache->rootConstantValid[ROOT_PARAM::SurfaceGpuSceneControl] = false;
    }
}

void BindSurfaceFeatureData(const MeshBindingContext& ctx, D3D12_GPU_VIRTUAL_ADDRESS featureDataAddress) {
    BindSrvCached(ctx, ROOT_PARAM::SurfaceFeatureData, featureDataAddress);
}

void BindTerrainSurfaceData(const MeshBindingContext& ctx, D3D12_GPU_VIRTUAL_ADDRESS terrainDataAddress) {
    BindSrvCached(ctx, ROOT_PARAM::TerrainSurfaceData, terrainDataAddress);
}

void BindOceanSpectrumData(const MeshBindingContext& ctx, D3D12_GPU_DESCRIPTOR_HANDLE spectrumTable, bool valid,
                           uint32_t resolution) {
    if (ctx.cmd == nullptr) {
        return;
    }

    // The spectrum sample seam short-circuits when the control flag is
    // zero. Keep the descriptor-table root parameter legal in that mode
    // by reusing an already-owned neutral SRV instead of allocating a
    // second six-descriptor table during an initialization failure.
    const bool outputTableValid = valid && spectrumTable.ptr != 0u;
    if (spectrumTable.ptr == 0u) {
        const D3D12_GPU_DESCRIPTOR_HANDLE fallbackSrv =
            RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(ctx.fallbackTextureHandle);
        if (fallbackSrv.ptr != 0u) {
            spectrumTable = fallbackSrv;
            if (ctx.graphics != nullptr && ctx.graphics->device != nullptr && ctx.graphics->srvHeap != nullptr) {
                const UINT fallbackIndex =
                    RENDER3D::GetTextureResourceSrvDescriptorIndexFromBackendHandle(ctx.fallbackTextureHandle);
                const bool hasSixDescriptorsInHeap =
                    fallbackIndex != UINT32_MAX && fallbackIndex <= GFX::DESCRIPTOR::kSrvHeapCapacity - 6u;
                if (!hasSixDescriptorsInHeap) {
                    spectrumTable = {};
                }
            }
        }
    }
    if (spectrumTable.ptr == 0u && ctx.graphics != nullptr && ctx.graphics->device != nullptr &&
        ctx.graphics->srvHeap != nullptr) {
        const UINT descriptorSize =
            ctx.graphics->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        // The fixed system range starts with six contiguous SRV slots.
        // Their contents are irrelevant while the control flag is zero,
        // but the table base remains inside the shader-visible heap.
        spectrumTable = GFX::DESCRIPTOR::GpuAt(ctx.graphics->srvHeap, descriptorSize,
                                               GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::SceneColorFrame0));
    }
    if (spectrumTable.ptr != 0u) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::OceanSpectrumData, spectrumTable);
    }
    const uint32_t constants[2] = {outputTableValid ? 1u : 0u, outputTableValid ? resolution : 0u};
    ctx.cmd->SetGraphicsRoot32BitConstants(ROOT_PARAM::OceanSpectrumControl, 2u, constants, 0u);
    if (ctx.cache != nullptr) {
        ctx.cache->rootConstantValid[ROOT_PARAM::OceanSpectrumControl] = false;
    }
}

void BindScreenSpaceTraceResources(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }

    const RENDER3D::DEPTH::DepthPyramidView& depthPyramid = ctx.passResources.depthPyramid;
    const bool traceInputValid =
        ctx.depthAwarePhase && ctx.passResources.sceneDepth.IsSampleable() &&
        ctx.passResources.opaqueSceneColor.IsSampleable() && ctx.passResources.packedSceneSurface.IsSampleable() &&
        depthPyramid.valid && depthPyramid.sourceKind == RENDER3D::DEPTH::DepthPyramidSourceKind::SceneDepth &&
        depthPyramid.viewKind == RENDER3D::DEPTH::DepthPyramidViewKind::CurrentFrame &&
        depthPyramid.depthConvention == RENDER3D::DEPTH::DepthPyramidDepthConvention::StandardD3DLessMax &&
        depthPyramid.pyramidSrv.ptr != 0 && depthPyramid.width > 0u && depthPyramid.height > 0u &&
        depthPyramid.mipCount > 0u && depthPyramid.sourceWidth == ctx.passResources.sceneDepth.width &&
        depthPyramid.sourceHeight == ctx.passResources.sceneDepth.height &&
        ctx.passResources.packedSceneSurface.width == ctx.passResources.sceneDepth.width &&
        ctx.passResources.packedSceneSurface.height == ctx.passResources.sceneDepth.height;

    D3D12_GPU_DESCRIPTOR_HANDLE fallbackSrv =
        RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(ctx.fallbackTextureHandle);
    if (fallbackSrv.ptr == 0) {
        fallbackSrv = ctx.passResources.sceneDepth.srv;
    }
    const D3D12_GPU_DESCRIPTOR_HANDLE depthPyramidSrv = traceInputValid ? depthPyramid.pyramidSrv : fallbackSrv;
    const D3D12_GPU_DESCRIPTOR_HANDLE packedSurfaceSrv =
        traceInputValid ? ctx.passResources.packedSceneSurface.srv : fallbackSrv;
    if (depthPyramidSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::ScreenSpaceDepthPyramid, depthPyramidSrv);
    }
    if (packedSurfaceSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::ScreenSpacePackedSurface, packedSurfaceSrv);
    }
    const bool rayShadowValid = ctx.passResources.rayTracedShadow.IsSampleable() &&
                                ctx.passResources.rayTracedShadow.width == ctx.passResources.sceneDepth.width &&
                                ctx.passResources.rayTracedShadow.height == ctx.passResources.sceneDepth.height;
    const D3D12_GPU_DESCRIPTOR_HANDLE rayShadowSrv =
        rayShadowValid ? ctx.passResources.rayTracedShadow.srv : fallbackSrv;
    if (rayShadowSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::RayTracedShadow, rayShadowSrv);
    }

    const bool boundTraceValid = traceInputValid && depthPyramidSrv.ptr != 0 && packedSurfaceSrv.ptr != 0;
    constexpr uint32_t kDepthTraceValid = 1u << 0;
    constexpr uint32_t kSceneColorCaptureValid = 1u << 1;
    constexpr uint32_t kRayShadowValid = 1u << 2;
    const bool sceneColorCaptureValid = ctx.passResources.opaqueSceneColor.IsSampleable();
    const uint32_t constants[2] = {(boundTraceValid ? kDepthTraceValid : 0u) |
                                       (sceneColorCaptureValid ? kSceneColorCaptureValid : 0u) |
                                       (rayShadowValid && rayShadowSrv.ptr != 0 ? kRayShadowValid : 0u),
                                   boundTraceValid ? depthPyramid.mipCount : 0u};
    ctx.cmd->SetGraphicsRoot32BitConstants(ROOT_PARAM::ScreenSpaceTraceControl, 2u, constants, 0u);
    if (ctx.cache != nullptr) {
        ctx.cache->rootConstantValid[ROOT_PARAM::ScreenSpaceTraceControl] = false;
    }
}

void BindSurfaceFeatureFloats(const MeshBindingContext& ctx, D3D12_GPU_VIRTUAL_ADDRESS featureFloatsAddress) {
    BindSrvCached(ctx, ROOT_PARAM::SurfaceFeatureFloats, featureFloatsAddress);
}

void BindMaterialTexturePool(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE texturePoolSrv = ctx.graphics != nullptr
                                                           ? RENDER3D::GetMaterialTexturePoolSrvGpuHandle(*ctx.graphics)
                                                           : D3D12_GPU_DESCRIPTOR_HANDLE{};
    if (texturePoolSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::TexturePool, texturePoolSrv);
    }
}

void BindClusterGeometryPool(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE clusterPoolSrv = ctx.graphics != nullptr
                                                           ? RENDER3D::GetClusterGeometryPoolSrvGpuHandle(*ctx.graphics)
                                                           : D3D12_GPU_DESCRIPTOR_HANDLE{};
    if (clusterPoolSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::ClusterGeometryPool, clusterPoolSrv);
    }
}
// マテリアルに関連するテクスチャセットをバインドする
void BindMeshletVisibleRanges(const MeshBindingContext& ctx, D3D12_GPU_VIRTUAL_ADDRESS visibleRangeAddress) {
    if (ctx.cmd == nullptr || visibleRangeAddress == 0 || ROOT_PARAM::MeshletVisibleRanges >= kTrackedRootParamCount) {
        return;
    }

    MeshBindingStateCache* cache = ctx.cache;
    if (cache != nullptr && cache->rootSignature == nullptr) {
        return;
    }
    if (cache != nullptr && cache->cbvAddresses[ROOT_PARAM::MeshletVisibleRanges] == visibleRangeAddress) {
        return;
    }

    ctx.cmd->SetGraphicsRootShaderResourceView(ROOT_PARAM::MeshletVisibleRanges, visibleRangeAddress);
    if (cache != nullptr) {
        cache->cbvAddresses[ROOT_PARAM::MeshletVisibleRanges] = visibleRangeAddress;
    }
}

void BindMeshletDeformationPalettes(const MeshBindingContext& ctx, D3D12_GPU_VIRTUAL_ADDRESS paletteAddress) {
    if (ctx.cmd == nullptr || paletteAddress == 0u) {
        return;
    }
    ctx.cmd->SetGraphicsRootShaderResourceView(ROOT_PARAM::JointPalette, paletteAddress);
    if (ctx.cache != nullptr) {
        ctx.cache->cbvAddresses[ROOT_PARAM::JointPalette] = paletteAddress;
    }
}

void BindPipelineState(const MeshBindingContext& ctx, ID3D12PipelineState* pso) {
    if (ctx.cmd == nullptr || pso == nullptr) {
        return;
    }

    MeshBindingStateCache* cache = ctx.cache;
    if (cache != nullptr && cache->pipelineState == pso) {
        return;
    }

    ctx.cmd->SetPipelineState(pso);
    if (cache != nullptr) {
        cache->pipelineState = pso;
    }
}

// システムテクスチャは材質テクスチャとは分けて束縛する。
// シャドウマップは材質ではなくシステムリソースとして束縛する。
void BindShadowMap(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv = SHADOW::GetDirectionalShadowSrv();
    if (shadowSrv.ptr == 0) {
        shadowSrv = RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(ctx.fallbackTextureHandle);
    }
    if (shadowSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::ShadowMap, shadowSrv);
    }
}

// スカイキューブマップを束縛する。
void BindSkyCube(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE skyCubeSrv = ResolveSkyCubeSrv(ctx.fallbackTextureHandle);
    if (skyCubeSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::SkyCube, skyCubeSrv);
    }
}
// シーンの深度テクスチャをバインドする。深度アウェアな描画フェーズの場合、サービスコンテキストからシーン深度SRVを取得し、利用可能であればそれを使用する。そうでない場合は、フォールバックテクスチャが使用される。
void BindSceneDepth(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv =
        ResolveSceneDepthSrv(ctx.depthAwarePhase, ctx.passResources.sceneDepth, ctx.fallbackTextureHandle);
    if (sceneDepthSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::SceneDepth, sceneDepthSrv);
    }
}
// シーンのカラー（アルベド）テクスチャをバインドする。ポストシステムからシーンカラーSRVを取得し、利用可能であればそれを使用する。そうでない場合は、フォールバックテクスチャが使用される。
void BindSceneColor(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv =
        ResolveSceneColorSrv(ctx.passResources.opaqueSceneColor, ctx.fallbackTextureHandle);
    if (sceneColorSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::SceneColor, sceneColorSrv);
    }
}

void BindIblResources(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }
    // IBL resource binding の境界を PIX 上で追えるようにする。
    GFX::PIX::ScopedGpuEvent pixIbl(ctx.cmd, GFX::PIX::kColorRender, "IBL.BindResources");

    const D3D12_GPU_DESCRIPTOR_HANDLE irradianceSrv = ResolveIblIrradianceSrv(ctx.fallbackTextureHandle);
    if (irradianceSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::IblIrradiance, irradianceSrv);
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE prefilteredSrv = ResolveIblPrefilteredSrv(ctx.fallbackTextureHandle);
    if (prefilteredSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::IblPrefiltered, prefilteredSrv);
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE brdfLutSrv = ResolveIblBrdfLutSrv(ctx.fallbackTextureHandle);
    if (brdfLutSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::IblBrdfLut, brdfLutSrv);
    }
}

void BindReflectionProbeResources(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }
    GFX::PIX::ScopedGpuEvent pixProbe(ctx.cmd, GFX::PIX::kColorRender, "ReflectionProbe.BindResources");

    const D3D12_GPU_DESCRIPTOR_HANDLE prefilteredSrv =
        ResolveReflectionProbePrefilteredSrv(0u, ctx.fallbackCubeTextureHandle);
    if (prefilteredSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::ReflectionProbePrefiltered, prefilteredSrv);
    }
    const D3D12_GPU_DESCRIPTOR_HANDLE secondarySrv =
        ResolveReflectionProbePrefilteredSrv(1u, ctx.fallbackCubeTextureHandle);
    if (secondarySrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::ReflectionProbePrefilteredSecondary, secondarySrv);
    }
}

void BindSsao(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE aoSrv =
        ResolveSsaoSrv(ctx.passResources.ambientOcclusion, ctx.passResources.fallbackAoTextureHandle);
    if (aoSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::Ssao, aoSrv);
    }
}

void BindLightProbeResources(const MeshBindingContext& ctx) {
    if (ctx.cmd == nullptr) {
        return;
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE shSrv = ResolveLightProbeShSrv(0u);
    if (shSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::LightProbeSh, shSrv);
    } else {
        static bool sWarnedMissingLightProbeSrv = false;
        if (!sWarnedMissingLightProbeSrv) {
            HIKARI_LOG_WARN("[LightProbe] missing SH volume SRV table.");
            sWarnedMissingLightProbeSrv = true;
        }
    }
    const D3D12_GPU_DESCRIPTOR_HANDLE secondaryShSrv = ResolveLightProbeShSrv(1u);
    if (secondaryShSrv.ptr != 0) {
        BindDescriptorTableCached(ctx, ROOT_PARAM::LightProbeShSecondary, secondaryShSrv);
    }
}

} // namespace HIKARI::MESHRENDERER
