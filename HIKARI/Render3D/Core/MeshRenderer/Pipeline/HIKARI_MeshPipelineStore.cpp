#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"

#include <cstddef>
#include <span>

#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshInputLayouts.h"

namespace HIKARI::MESHRENDERER {

    using Microsoft::WRL::ComPtr;

    size_t VariantKeyHasher::operator()(const VFX::VariantKey& key) const noexcept {
        size_t seed = std::hash<std::string>{}(key.shaderId);
        seed ^= std::hash<std::string>{}(key.vertexShaderId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<std::string>{}(key.pixelShaderId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= static_cast<size_t>(key.featureBits) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= static_cast<size_t>(key.composite) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= static_cast<size_t>(key.depthTest) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= static_cast<size_t>(key.depthWrite) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= static_cast<size_t>(key.doubleSided) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }

    namespace {

        D3D12_SHADER_BYTECODE CookedBytecode(
            const ASSETS::SHADER::ShaderAssetEntryLease& lease) noexcept {
            const std::span<const uint8_t> bytes = lease.GetDxil();
            return { bytes.data(), bytes.size() };
        }

		// シェーダIDから、実際のシェーダファイルのパスを解決する。特定のIDに対しては、デフォルトのファイルパスを返す。
		// シェーダIDをキーにして、コンパイル済みのピクセルシェーダのバイナリをキャッシュから取得する。キャッシュに存在しない場合は、ファイルからコンパイルしてキャッシュに保存する。
        bool ResolvePixelShaderBytecode(
            MeshPipelineStore& store,
            const std::string& shaderProfileId,
            D3D12_SHADER_BYTECODE& outBytecode) {
            const std::string cacheKey = shaderProfileId.empty() ? "StaticLit" : shaderProfileId;
            if (cacheKey == "PBR" || cacheKey == "StaticLit") {
                outBytecode = CookedBytecode(store.psShader);
                return outBytecode.pShaderBytecode != nullptr;
            }
            if (cacheKey == "StaticFx" || cacheKey == "MaterialFx" ||
                cacheKey == "Render3D_StaticFxPS" ||
                cacheKey ==
                    "HIKARI.EngineShader.Render3D_StaticFxPS") {
                outBytecode = CookedBytecode(store.materialFxPsShader);
                return outBytecode.pShaderBytecode != nullptr;
            }
            DEBUGLOG::PushRenderError(
                std::string("[MeshRenderer][MaterialFx][WARN] Uncooked pixel ") +
                "shader rejected. shaderId=" + cacheKey);
            return false;
        }
        // シェーダIDをキーにして、コンパイル済みの頂点シェーダのバイナリをキャッシュから取得する。キャッシュに存在しない場合は、ファイルからコンパイルしてキャッシュに保存する。
        bool ResolveVertexShaderBytecode(
            MeshPipelineStore& store,
            const std::string& vertexShaderId,
            D3D12_SHADER_BYTECODE& outBytecode) {
            const std::string cacheKey = vertexShaderId.empty() ? "Render3D_StaticVS" : vertexShaderId;
            if (cacheKey == "Render3D_StaticVS") {
                outBytecode = CookedBytecode(store.vsShader);
                return outBytecode.pShaderBytecode != nullptr;
            }
            DEBUGLOG::PushRenderError(
                std::string("[MeshRenderer][MaterialFx][WARN] Uncooked vertex ") +
                "shader rejected. shaderId=" + cacheKey);
            return false;
        }
        
		// メッシュの描画に使用するパイプラインステートオブジェクトを作成する。頂点シェーダとピクセルシェーダのバイナリを取得し、入力レイアウトやラスタライザステートなどを設定して、ID3D12PipelineStateを生成する。
        bool CreateVariantPipeline(MeshPipelineStore& store, ID3D12Device* device, const VFX::VariantKey& key, bool wireframe, ID3D12PipelineState** outPso) {
            D3D12_SHADER_BYTECODE vertexShader{};
            if (!ResolveVertexShaderBytecode(
                    store,
                    key.vertexShaderId,
                    vertexShader) ||
                vertexShader.pShaderBytecode == nullptr) {
                vertexShader = CookedBytecode(store.vsShader);
            }
            D3D12_SHADER_BYTECODE pixelShader{};
            const std::string psId = !key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId;
            if (!ResolvePixelShaderBytecode(store, psId, pixelShader) ||
                pixelShader.pShaderBytecode == nullptr) {
                return false;
            }

            const auto inputElements = GetStaticMeshInputLayout();

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = store.rootSig.Get();
            psoDesc.VS = vertexShader;
            psoDesc.PS = pixelShader;
            ApplyCompositeBlendState(psoDesc.BlendState, key.composite);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FillMode = wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = key.doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = key.depthTest ? TRUE : FALSE;
            psoDesc.DepthStencilState.DepthWriteMask = key.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { inputElements.data(), static_cast<UINT>(inputElements.size()) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;
            return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(outPso)));
        }

        bool CreateSkinnedVariantPipeline(MeshPipelineStore& store, ID3D12Device* device, const VFX::VariantKey& key, bool wireframe, ID3D12PipelineState** outPso) {
            D3D12_SHADER_BYTECODE pixelShader{};
            const std::string psId = !key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId;
            const D3D12_SHADER_BYTECODE vertexShader =
                CookedBytecode(store.skinnedVsShader);
            if (!ResolvePixelShaderBytecode(store, psId, pixelShader) ||
                pixelShader.pShaderBytecode == nullptr ||
                vertexShader.pShaderBytecode == nullptr) {
                return false;
            }
            if (!key.vertexShaderId.empty()) {
                DEBUGLOG::PushRenderError(std::string("[MeshRenderer][MaterialFx][WARN] Custom vertexShaderId is ignored for skinned mesh. vertexShaderId=") + key.vertexShaderId);
            }

            const auto inputElements = GetSkinnedMeshInputLayout();

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = store.skinnedRootSig.Get();
            psoDesc.VS = vertexShader;
            psoDesc.PS = pixelShader;
            ApplyCompositeBlendState(psoDesc.BlendState, key.composite);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FillMode = wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = key.doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = key.depthTest ? TRUE : FALSE;
            psoDesc.DepthStencilState.DepthWriteMask = key.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { inputElements.data(), static_cast<UINT>(inputElements.size()) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;
            return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(outPso)));
        }

        template <typename PipelineCache>
        void RetirePipelineCache(PipelineCache& cache, const char* debugName) {
            for (auto& entry : cache) {
                ComPtr<ID3D12PipelineState>& pso = entry.second;
                if (pso == nullptr) {
                    continue;
                }

                ID3D12PipelineState* retired = pso.Detach();
                GFX::RetireD3D12ObjectForCurrentFrame(
                    retired,
                    debugName != nullptr ? debugName : "MeshRenderer.VariantPSO");
            }
            cache.clear();
        }

    }

    void ApplyCompositeBlendState(D3D12_BLEND_DESC& blendState, VFX::CompositeMode composite) {
        blendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

        D3D12_RENDER_TARGET_BLEND_DESC& rt0 = blendState.RenderTarget[0];

        if (composite == VFX::CompositeMode::Alpha) {
            rt0.BlendEnable = TRUE;
            // StaticPS normalizes every transparent material to premultiplied
            // output before it reaches this pipeline.
            rt0.SrcBlend = D3D12_BLEND_ONE;
            rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            rt0.BlendOp = D3D12_BLEND_OP_ADD;
            rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        } else if (composite == VFX::CompositeMode::Additive) {
            rt0.BlendEnable = TRUE;
            rt0.SrcBlend = D3D12_BLEND_ONE;
            rt0.DestBlend = D3D12_BLEND_ONE;
            rt0.BlendOp = D3D12_BLEND_OP_ADD;
            rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt0.DestBlendAlpha = D3D12_BLEND_ONE;
            rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        } else if (composite == VFX::CompositeMode::Multiply){
			rt0.BlendEnable = TRUE;
			rt0.SrcBlend = D3D12_BLEND_DEST_COLOR;
			rt0.DestBlend = D3D12_BLEND_ZERO;
			rt0.BlendOp = D3D12_BLEND_OP_ADD;
			rt0.SrcBlendAlpha = D3D12_BLEND_DEST_ALPHA;
			rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
    }

    void ShutdownMeshPipelines(MeshPipelineStore& store) {
        InvalidateMeshPipelineVariants(store);
        GFX::RetireD3D12ObjectForCurrentFrame(store.pso, "MeshRenderer.StaticPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.skinnedPso, "MeshRenderer.SkinnedPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.depthPso, "MeshRenderer.DepthPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.depthSkinnedPso, "MeshRenderer.DepthSkinnedPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.waterDepthPso, "MeshRenderer.WaterDepthPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.waterDepthSkinnedPso, "MeshRenderer.WaterDepthSkinnedPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.geometryPso, "MeshRenderer.GeometryPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.geometrySkinnedPso, "MeshRenderer.GeometrySkinnedPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.geometryFeatureMaskPso, "MeshRenderer.GeometryFeatureMaskPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.geometryFeatureMaskSkinnedPso, "MeshRenderer.GeometryFeatureMaskSkinnedPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.geometryOutlinePso, "MeshRenderer.GeometryOutlinePSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.geometryOutlineSkinnedPso, "MeshRenderer.GeometryOutlineSkinnedPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.visibilityOverlayPso, "MeshRenderer.VisibilityOverlayPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.visibilityOverlaySkinnedPso, "MeshRenderer.VisibilityOverlaySkinnedPSO");
        GFX::RetireD3D12ObjectForCurrentFrame(store.rootSig, "MeshRenderer.StaticRootSignature");
        GFX::RetireD3D12ObjectForCurrentFrame(store.skinnedRootSig, "MeshRenderer.SkinnedRootSignature");
        store.vsShader = {};
        store.skinnedVsShader = {};
        store.psShader = {};
        store.materialFxPsShader = {};
        store.waterDepthPsShader = {};
        store.geometryPsShader = {};
        store.geometryFeatureMaskPsShader = {};
        store.geometryOutlineVsShader = {};
        store.geometryOutlineSkinnedVsShader = {};
        store.geometryOutlinePsShader = {};
        store.visibilityOverlayPsShader = {};
    }

    void InvalidateMeshPipelineVariants(MeshPipelineStore& store) {
        RetirePipelineCache(store.variantPsoCache, "MeshRenderer.StaticVariantPSO");
        RetirePipelineCache(store.skinnedVariantPsoCache, "MeshRenderer.SkinnedVariantPSO");
        RetirePipelineCache(store.wireVariantPsoCache, "MeshRenderer.WireVariantPSO");
        RetirePipelineCache(store.skinnedWireVariantPsoCache, "MeshRenderer.SkinnedWireVariantPSO");
    }

    ID3D12RootSignature* GetStaticRootSignature(MeshPipelineStore& store) {
        return store.rootSig.Get();
    }

    ID3D12RootSignature* GetSkinnedRootSignature(MeshPipelineStore& store) {
        return store.skinnedRootSig.Get();
    }

    ID3D12PipelineState* GetDepthPso(MeshPipelineStore& store, bool skinned) {
        return skinned ? store.depthSkinnedPso.Get() : store.depthPso.Get();
    }

    ID3D12PipelineState* GetWaterDepthPso(
        MeshPipelineStore& store,
        bool skinned) {
        return skinned
            ? store.waterDepthSkinnedPso.Get()
            : store.waterDepthPso.Get();
    }

    ID3D12PipelineState* GetGeometryPso(
        MeshPipelineStore& store,
        bool skinned,
        bool featureMask) {

        if (featureMask) {
            return skinned
                ? store.geometryFeatureMaskSkinnedPso.Get()
                : store.geometryFeatureMaskPso.Get();
        }
        return skinned ? store.geometrySkinnedPso.Get() : store.geometryPso.Get();
    }

    ID3D12PipelineState* GetGeometryOutlinePso(
        MeshPipelineStore& store,
        bool skinned) {

        return skinned
            ? store.geometryOutlineSkinnedPso.Get()
            : store.geometryOutlinePso.Get();
    }

    ID3D12PipelineState* GetVisibilityOverlayPso(
        MeshPipelineStore& store,
        bool skinned) {

        return skinned
            ? store.visibilityOverlaySkinnedPso.Get()
            : store.visibilityOverlayPso.Get();
    }

    ID3D12PipelineState* GetOrCreateVariantPso(
        MeshPipelineStore& store,
        ID3D12Device* device,
        MeshRendererDebugStats& stats,
        const VFX::VariantKey& key,
        bool skinned,
        bool wireframe) {
        auto& cache = skinned
            ? (wireframe ? store.skinnedWireVariantPsoCache : store.skinnedVariantPsoCache)
            : (wireframe ? store.wireVariantPsoCache : store.variantPsoCache);

        auto found = cache.find(key);
        if (found == cache.end()) {
            ++stats.psoCacheMissCount;
            ComPtr<ID3D12PipelineState> variantPso;
            const bool created = skinned
                ? CreateSkinnedVariantPipeline(store, device, key, wireframe, variantPso.GetAddressOf())
                : CreateVariantPipeline(store, device, key, wireframe, variantPso.GetAddressOf());
            if (!created) {
                DEBUGLOG::PushRenderError(
                    std::string("[MeshRenderer][MaterialFx][WARN] Variant PSO create failed; fallback used. vs=") +
                    (key.vertexShaderId.empty() ? "<default>" : key.vertexShaderId) +
                    " ps=" +
                    ((!key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId).empty()
                        ? "<default>"
                        : (!key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId)) +
                    " skinned=" +
                    (skinned ? "yes" : "no") +
                    " wireframe=" +
                    (wireframe ? "yes" : "no"));
                variantPso = skinned ? (store.skinnedPso ? store.skinnedPso : store.pso) : store.pso;
            }
            found = cache.emplace(key, std::move(variantPso)).first;
        } else {
            ++stats.psoCacheHitCount;
        }

        return found->second.Get();
    }

} // namespace HIKARI::MESHRENDERER
