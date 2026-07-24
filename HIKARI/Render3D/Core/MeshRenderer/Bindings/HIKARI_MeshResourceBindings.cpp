#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"

#include <algorithm>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "HIKARI_Services.h"
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

        void BindRootSignatureCached(
            const MeshBindingContext& ctx,
            ID3D12RootSignature* rootSig) {

            if (ctx.cmd == nullptr || rootSig == nullptr) {
                return;
            }

            MeshBindingStateCache* cache = ctx.cache;
            MeshRendererDebugStats* stats = ctx.stats;
            if (cache != nullptr && cache->rootSignature == rootSig) {
                if (stats != nullptr) {
                    ++stats->rootSignatureSkipCount;
                }
                return;
            }

            ctx.cmd->SetGraphicsRootSignature(rootSig);
            if (stats != nullptr) {
                ++stats->rootSignatureBindCount;
            }
            if (cache != nullptr) {
                cache->rootSignature = rootSig;
                // root signature 変更時は root 引数を再設定する。
                ResetRootParamCache(*cache);
            }
        }

        void BindCbvCached(
            const MeshBindingContext& ctx,
            UINT rootParam,
            D3D12_GPU_VIRTUAL_ADDRESS address,
            bool objectScoped) {

            if (ctx.cmd == nullptr || rootParam >= kTrackedRootParamCount) {
                return;
            }

            MeshBindingStateCache* cache = ctx.cache;
            if (cache != nullptr && cache->rootSignature == nullptr) {
                return;
            }
            MeshRendererDebugStats* stats = ctx.stats;
            if (cache != nullptr && cache->cbvAddresses[rootParam] == address) {
                if (stats != nullptr) {
                    if (objectScoped) {
                        ++stats->objectResourceSkipCount;
                    } else {
                        ++stats->frameResourceSkipCount;
                    }
                }
                return;
            }

            ctx.cmd->SetGraphicsRootConstantBufferView(rootParam, address);
            if (stats != nullptr) {
                if (objectScoped) {
                    ++stats->objectResourceBindCount;
                } else {
                    ++stats->frameResourceBindCount;
                }
            }
            if (cache != nullptr) {
                cache->cbvAddresses[rootParam] = address;
            }
        }

        bool BindDescriptorTableCached(
            const MeshBindingContext& ctx,
            UINT rootParam,
            D3D12_GPU_DESCRIPTOR_HANDLE handle) {

            if (ctx.cmd == nullptr || rootParam >= kTrackedRootParamCount || handle.ptr == 0) {
                return false;
            }

            MeshBindingStateCache* cache = ctx.cache;
            if (cache != nullptr && cache->rootSignature == nullptr) {
                return false;
            }
            MeshRendererDebugStats* stats = ctx.stats;
            if (cache != nullptr && cache->descriptorTables[rootParam].ptr == handle.ptr) {
                if (stats != nullptr) {
                    ++stats->descriptorTableSkipCount;
                }
                return false;
            }

            ctx.cmd->SetGraphicsRootDescriptorTable(rootParam, handle);
            if (stats != nullptr) {
                ++stats->descriptorTableBindCount;
            }
            if (cache != nullptr) {
                cache->descriptorTables[rootParam] = handle;
            }
            return true;
        }

        void BindObjectDataBufferCached(
            const MeshBindingContext& ctx,
            D3D12_GPU_DESCRIPTOR_HANDLE handle) {

            if (ctx.cmd == nullptr || handle.ptr == 0) {
                return;
            }

            MeshBindingStateCache* cache = ctx.cache;
            if (cache != nullptr && cache->rootSignature == nullptr) {
                return;
            }
            MeshRendererDebugStats* stats = ctx.stats;
            if (cache != nullptr &&
                cache->descriptorTables[ROOT_PARAM::ObjectData].ptr == handle.ptr) {
                if (stats != nullptr) {
                    ++stats->objectDataBufferSkipCount;
                }
                return;
            }

            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::ObjectData, handle);
            if (stats != nullptr) {
                ++stats->objectDataBufferBindCount;
            }
            if (cache != nullptr) {
                cache->descriptorTables[ROOT_PARAM::ObjectData] = handle;
            }
        }

        void BindMaterialDataBufferCached(
            const MeshBindingContext& ctx,
            D3D12_GPU_DESCRIPTOR_HANDLE handle) {

            if (ctx.cmd == nullptr || handle.ptr == 0) {
                return;
            }

            MeshBindingStateCache* cache = ctx.cache;
            if (cache != nullptr && cache->rootSignature == nullptr) {
                return;
            }
            MeshRendererDebugStats* stats = ctx.stats;
            if (cache != nullptr &&
                cache->descriptorTables[ROOT_PARAM::MaterialData].ptr == handle.ptr) {
                if (stats != nullptr) {
                    ++stats->materialDataBufferSkipCount;
                }
                return;
            }

            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::MaterialData, handle);
            if (stats != nullptr) {
                ++stats->materialDataBufferBindCount;
            }
            if (cache != nullptr) {
                cache->descriptorTables[ROOT_PARAM::MaterialData] = handle;
            }
        }

        void BindSurfaceGpuSceneBufferCached(
            const MeshBindingContext& ctx,
            D3D12_GPU_DESCRIPTOR_HANDLE handle) {

            if (ctx.cmd == nullptr ||
                handle.ptr == 0 ||
                ROOT_PARAM::SurfaceGpuScene >= kTrackedRootParamCount) {
                return;
            }

            MeshBindingStateCache* cache = ctx.cache;
            if (cache != nullptr && cache->rootSignature == nullptr) {
                return;
            }
            MeshRendererDebugStats* stats = ctx.stats;
            if (cache != nullptr &&
                cache->descriptorTables[ROOT_PARAM::SurfaceGpuScene].ptr == handle.ptr) {
                if (stats != nullptr) {
                    ++stats->surfaceGpuSceneBufferSkipCount;
                }
                return;
            }

            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::SurfaceGpuScene, handle);
            if (stats != nullptr) {
                ++stats->surfaceGpuSceneBufferBindCount;
            }
            if (cache != nullptr) {
                cache->descriptorTables[ROOT_PARAM::SurfaceGpuScene] = handle;
            }
        }

        void BindRootConstantCached(
            const MeshBindingContext& ctx,
            UINT rootParam,
            uint32_t value) {

            if (ctx.cmd == nullptr || rootParam >= kTrackedRootParamCount) {
                return;
            }

            MeshBindingStateCache* cache = ctx.cache;
            if (cache != nullptr && cache->rootSignature == nullptr) {
                return;
            }
            MeshRendererDebugStats* stats = ctx.stats;
            if (cache != nullptr &&
                cache->rootConstantValid[rootParam] &&
                cache->rootConstants[rootParam] == value) {
                if (stats != nullptr) {
                    if (rootParam == ROOT_PARAM::ObjectIndex) {
                        ++stats->objectIndexSkipCount;
                    } else if (rootParam == ROOT_PARAM::MaterialIndex) {
                        ++stats->materialIndexSkipCount;
                    }
                }
                return;
            }

            ctx.cmd->SetGraphicsRoot32BitConstant(rootParam, value, 0);
            if (stats != nullptr) {
                if (rootParam == ROOT_PARAM::ObjectIndex) {
                    ++stats->objectIndexBindCount;
                } else if (rootParam == ROOT_PARAM::MaterialIndex) {
                    ++stats->materialIndexBindCount;
                }
            }
            if (cache != nullptr) {
                cache->rootConstants[rootParam] = value;
                cache->rootConstantValid[rootParam] = true;
            }
        }

    }
	// フレーム全体で共通のリソースをバインドする。これには、カメラ、ライト、シャドウ、スカイ環境の定数バッファが含まれる。ルートシグネチャも設定される。
    void BindFrameCommonResources(
        const MeshBindingContext& ctx,
        ID3D12RootSignature* rootSig,
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress,
        D3D12_GPU_VIRTUAL_ADDRESS cullingCameraAddress,
        D3D12_GPU_VIRTUAL_ADDRESS lightAddress,
        D3D12_GPU_VIRTUAL_ADDRESS shadowAddress,
        D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress) {
        if (ctx.cmd == nullptr || rootSig == nullptr) {
            return;
        }

        BindRootSignatureCached(ctx, rootSig);
        BindCbvCached(ctx, ROOT_PARAM::Camera, cameraAddress, false);
        BindCbvCached(ctx, ROOT_PARAM::CullingCamera, cullingCameraAddress, false);
        BindCbvCached(ctx, ROOT_PARAM::Light, lightAddress, false);
        BindCbvCached(ctx, ROOT_PARAM::ShadowCB, shadowAddress, false);
        BindCbvCached(ctx, ROOT_PARAM::SkyEnvironment, skyEnvironmentAddress, false);
        BindMaterialTexturePool(ctx);
        BindClusterGeometryPool(ctx);
    }
	// オブジェクト固有の定数バッファをバインドする。これには、モデル行列やマテリアルプロパティなどが含まれる。ルートパラメータのObjectスロットにバインドされる。
    void BindObjectConstantBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS objectAddress) {
        if (ctx.cmd == nullptr) {
            return;
        }

        BindCbvCached(ctx, ROOT_PARAM::Object, objectAddress, true);
    }

    void BindObjectDataBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrv) {
        BindObjectDataBufferCached(ctx, objectDataSrv);
    }

    void BindObjectDataIndex(
        const MeshBindingContext& ctx,
        uint32_t objectIndex) {
        BindRootConstantCached(ctx, ROOT_PARAM::ObjectIndex, objectIndex);
    }

    void BindMaterialDataBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrv) {
        BindMaterialDataBufferCached(ctx, materialDataSrv);
    }

    void BindMaterialDataIndex(
        const MeshBindingContext& ctx,
        uint32_t materialIndex) {
        BindRootConstantCached(ctx, ROOT_PARAM::MaterialIndex, materialIndex);
    }

    void BindSurfaceGpuSceneBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrv) {
        BindSurfaceGpuSceneBufferCached(ctx, surfaceGpuSceneSrv);
    }

    void BindSurfaceGpuSceneControl(
        const MeshBindingContext& ctx,
        uint32_t baseInstanceIndex,
        bool enabled) {
        if (ctx.cmd == nullptr ||
            ROOT_PARAM::SurfaceGpuSceneControl >= kTrackedRootParamCount) {
            return;
        }
        if (ctx.cache != nullptr && ctx.cache->rootSignature == nullptr) {
            return;
        }

        const uint32_t constants[4] = {
            baseInstanceIndex,
            enabled ? 1u : 0u,
            0u,
            0u
        };
        ctx.cmd->SetGraphicsRoot32BitConstants(
            ROOT_PARAM::SurfaceGpuSceneControl,
            4,
            constants,
            0);
        if (ctx.cache != nullptr) {
            ctx.cache->rootConstantValid[ROOT_PARAM::SurfaceGpuSceneControl] = false;
        }
    }

    void BindMaterialTexturePool(const MeshBindingContext& ctx) {
        if (ctx.cmd == nullptr) {
            return;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE texturePoolSrv = RENDER3D::GetMaterialTexturePoolSrvGpuHandle(SERVICES::gCtx);
        if (texturePoolSrv.ptr != 0) {
            BindDescriptorTableCached(ctx, ROOT_PARAM::TexturePool, texturePoolSrv);
        }
    }

    void BindClusterGeometryPool(const MeshBindingContext& ctx) {
        if (ctx.cmd == nullptr) {
            return;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE clusterPoolSrv = RENDER3D::GetClusterGeometryPoolSrvGpuHandle(SERVICES::gCtx);
        if (clusterPoolSrv.ptr != 0) {
            BindDescriptorTableCached(ctx, ROOT_PARAM::ClusterGeometryPool, clusterPoolSrv);
        }
    }
	// マテリアルに関連するテクスチャセットをバインドする
    void BindMeshletVisibleRanges(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS visibleRangeAddress) {

        if (ctx.cmd == nullptr ||
            visibleRangeAddress == 0 ||
            ROOT_PARAM::MeshletVisibleRanges >= kTrackedRootParamCount) {
            return;
        }

        MeshBindingStateCache* cache = ctx.cache;
        if (cache != nullptr && cache->rootSignature == nullptr) {
            return;
        }
        if (cache != nullptr &&
            cache->cbvAddresses[ROOT_PARAM::MeshletVisibleRanges] == visibleRangeAddress) {
            return;
        }

        ctx.cmd->SetGraphicsRootShaderResourceView(
            ROOT_PARAM::MeshletVisibleRanges,
            visibleRangeAddress);
        if (cache != nullptr) {
            cache->cbvAddresses[ROOT_PARAM::MeshletVisibleRanges] = visibleRangeAddress;
        }
    }

    void BindMeshletVisibleClusterList(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS visibleClusterListAddress) {

        if (ctx.cmd == nullptr ||
            visibleClusterListAddress == 0 ||
            ROOT_PARAM::MeshletVisibleClusterList >= kTrackedRootParamCount) {
            return;
        }

        MeshBindingStateCache* cache = ctx.cache;
        if (cache != nullptr && cache->rootSignature == nullptr) {
            return;
        }
        if (cache != nullptr &&
            cache->cbvAddresses[ROOT_PARAM::MeshletVisibleClusterList] ==
                visibleClusterListAddress) {
            return;
        }

        ctx.cmd->SetGraphicsRootShaderResourceView(
            ROOT_PARAM::MeshletVisibleClusterList,
            visibleClusterListAddress);
        if (cache != nullptr) {
            cache->cbvAddresses[ROOT_PARAM::MeshletVisibleClusterList] =
                visibleClusterListAddress;
        }
    }

    void BindMeshletDeformationPalettes(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS paletteAddress) {

        if (ctx.cmd == nullptr || paletteAddress == 0u) {
            return;
        }
        ctx.cmd->SetGraphicsRootShaderResourceView(
            ROOT_PARAM::JointPalette,
            paletteAddress);
        if (ctx.cache != nullptr) {
            ctx.cache->cbvAddresses[ROOT_PARAM::JointPalette] = paletteAddress;
        }
    }

    void BindPipelineState(
        const MeshBindingContext& ctx,
        ID3D12PipelineState* pso) {
        if (ctx.cmd == nullptr || pso == nullptr) {
            return;
        }

        MeshBindingStateCache* cache = ctx.cache;
        MeshRendererDebugStats* stats = ctx.stats;
        if (cache != nullptr && cache->pipelineState == pso) {
            if (stats != nullptr) {
                ++stats->pipelineStateSkipCount;
            }
            return;
        }

        ctx.cmd->SetPipelineState(pso);
        if (stats != nullptr) {
            ++stats->pipelineStateBindCount;
        }
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
            shadowSrv =
                RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(ctx.fallbackTextureHandle);
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
            ResolveSceneDepthSrv(
                ctx.depthAwarePhase,
                ctx.passResources.sceneDepthSrv,
                ctx.fallbackTextureHandle);
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
            ResolveSceneColorSrv(ctx.passResources.sceneColorSrv, ctx.fallbackTextureHandle);
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

        const D3D12_GPU_DESCRIPTOR_HANDLE irradianceSrv =
            ResolveIblIrradianceSrv(ctx.fallbackTextureHandle);
        if (irradianceSrv.ptr != 0) {
            BindDescriptorTableCached(ctx, ROOT_PARAM::IblIrradiance, irradianceSrv);
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE prefilteredSrv =
            ResolveIblPrefilteredSrv(ctx.fallbackTextureHandle);
        if (prefilteredSrv.ptr != 0) {
            BindDescriptorTableCached(ctx, ROOT_PARAM::IblPrefiltered, prefilteredSrv);
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE brdfLutSrv =
            ResolveIblBrdfLutSrv(ctx.fallbackTextureHandle);
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
            ResolveReflectionProbePrefilteredSrv(ctx.fallbackCubeTextureHandle);
        if (prefilteredSrv.ptr != 0) {
            BindDescriptorTableCached(ctx, ROOT_PARAM::ReflectionProbePrefiltered, prefilteredSrv);
        }
    }

    void BindSsao(const MeshBindingContext& ctx) {
        if (ctx.cmd == nullptr) {
            return;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE aoSrv =
            ResolveSsaoSrv(
                ctx.passResources.ssaoSrv,
                ctx.passResources.fallbackAoTextureHandle);
        if (aoSrv.ptr != 0) {
            BindDescriptorTableCached(ctx, ROOT_PARAM::Ssao, aoSrv);
        }
    }

    void BindLightProbeResources(const MeshBindingContext& ctx) {
        if (ctx.cmd == nullptr) {
            return;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE shSrv = ResolveLightProbeShSrv();
        if (shSrv.ptr != 0) {
            BindDescriptorTableCached(ctx, ROOT_PARAM::LightProbeSh, shSrv);
        } else {
            static bool sWarnedMissingLightProbeSrv = false;
            if (!sWarnedMissingLightProbeSrv) {
                HIKARI_LOG_WARN("[LightProbe] missing SH volume SRV table.");
                sWarnedMissingLightProbeSrv = true;
            }
        }
    }

} // namespace HIKARI::MESHRENDERER
