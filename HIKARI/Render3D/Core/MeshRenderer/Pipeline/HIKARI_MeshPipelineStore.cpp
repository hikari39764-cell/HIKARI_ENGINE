#include "Core/HIKARI_MathConfig.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"

#include <cstddef>
#include <span>

#include <d3dx12.h>

#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Assets/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Core/HIKARI_StartupTimer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshInputLayouts.h"

namespace HIKARI::MESHRENDERER {

using Microsoft::WRL::ComPtr;

size_t VariantKeyHasher::operator()(const MeshPipelineVariantQuery& variant) const noexcept {
    const auto& key = variant.material;
    size_t seed = std::hash<std::string>{}(key.shaderId);
    seed ^= std::hash<std::string>{}(key.vertexShaderId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<std::string>{}(key.pixelShaderId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= static_cast<size_t>(key.featureBits) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= static_cast<size_t>(key.composite) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= static_cast<size_t>(key.depthTest) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= static_cast<size_t>(key.depthWrite) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= static_cast<size_t>(key.doubleSided) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed ^ (static_cast<size_t>(variant.reflectionCapture) << 1) ^
           (static_cast<size_t>(variant.reflectionMaterialCapture) << 2) ^
           (static_cast<size_t>(variant.subsurfaceCapture) << 3);
}

namespace {

D3D12_SHADER_BYTECODE CookedBytecode(const ASSETS::SHADER::ShaderAssetEntryLease& lease) noexcept {
    const std::span<const uint8_t> bytes = lease.GetDxil();
    return {bytes.data(), bytes.size()};
}

// シェーダIDから、実際のシェーダファイルのパスを解決する。特定のIDに対しては、デフォルトのファイルパスを返す。
// シェーダIDをキーにして、コンパイル済みのピクセルシェーダのバイナリをキャッシュから取得する。キャッシュに存在しない場合は、ファイルからコンパイルしてキャッシュに保存する。
bool ResolvePixelShaderBytecode(MeshPipelineStore& store, const std::string& shaderProfileId,
                                D3D12_SHADER_BYTECODE& outBytecode, bool reflectionCapture) {
    const std::string cacheKey = shaderProfileId.empty() ? "StaticLit" : shaderProfileId;
    if (cacheKey == "PBR" || cacheKey == "StaticLit") {
        outBytecode = CookedBytecode(reflectionCapture ? store.reflectionPsShader : store.psShader);
        return outBytecode.pShaderBytecode != nullptr;
    }
    if (cacheKey == "StaticFx" || cacheKey == "MaterialFx" || cacheKey == "Render3D_StaticFxPS" ||
        cacheKey == "HIKARI.EngineShader.Render3D_StaticFxPS") {
        outBytecode = CookedBytecode(reflectionCapture ? store.reflectionFxPsShader : store.materialFxPsShader);
        return outBytecode.pShaderBytecode != nullptr;
    }
    DEBUGLOG::PushRenderError(std::string("[MeshRenderer][MaterialFx][WARN] Uncooked pixel ") +
                              "shader rejected. shaderId=" + cacheKey);
    return false;
}
// シェーダIDをキーにして、コンパイル済みの頂点シェーダのバイナリをキャッシュから取得する。キャッシュに存在しない場合は、ファイルからコンパイルしてキャッシュに保存する。
bool ResolveVertexShaderBytecode(MeshPipelineStore& store, const std::string& vertexShaderId,
                                 D3D12_SHADER_BYTECODE& outBytecode) {
    const std::string cacheKey = vertexShaderId.empty() ? "Render3D_StaticVS" : vertexShaderId;
    if (cacheKey == "Render3D_StaticVS") {
        outBytecode = CookedBytecode(store.vsShader);
        return outBytecode.pShaderBytecode != nullptr;
    }
    DEBUGLOG::PushRenderError(std::string("[MeshRenderer][MaterialFx][WARN] Uncooked vertex ") +
                              "shader rejected. shaderId=" + cacheKey);
    return false;
}

// メッシュの描画に使用するパイプラインステートオブジェクトを作成する。頂点シェーダとピクセルシェーダのバイナリを取得し、入力レイアウトやラスタライザステートなどを設定して、ID3D12PipelineStateを生成する。
bool CreateVariantPipeline(MeshPipelineStore& store, ID3D12Device* device, const VFX::VariantKey& key, bool wireframe,
                           bool reflectionCapture, bool reflectionMaterialCapture, bool subsurfaceCapture,
                           ID3D12PipelineState** outPso) {
    D3D12_SHADER_BYTECODE vertexShader{};
    if (!ResolveVertexShaderBytecode(store, key.vertexShaderId, vertexShader) ||
        vertexShader.pShaderBytecode == nullptr) {
        vertexShader = CookedBytecode(store.vsShader);
    }
    D3D12_SHADER_BYTECODE pixelShader{};
    const std::string psId = !key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId;
    if (!ResolvePixelShaderBytecode(store, psId, pixelShader, reflectionCapture) ||
        pixelShader.pShaderBytecode == nullptr) {
        return false;
    }

    const auto inputElements = GetStaticMeshInputLayout();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = store.rootSig.Get();
    psoDesc.VS = vertexShader;
    psoDesc.PS = pixelShader;
    ApplyCompositeBlendState(psoDesc.BlendState, key.composite, reflectionCapture);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.FrontCounterClockwise = MATH::kFrontFaceCounterClockwise ? TRUE : FALSE;
    psoDesc.RasterizerState.FillMode = wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = key.doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = key.depthTest ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask =
        key.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // capture は 5 出力（色・fallback 鏡面・鏡面応答・材質・次表面散乱の拡散光）。持たない出力は null RTV へ捨てる。
    psoDesc.NumRenderTargets = reflectionCapture ? 5 : 1;
    for (UINT i = 0; i < psoDesc.NumRenderTargets; ++i)
        psoDesc.RTVFormats[i] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (reflectionCapture && !reflectionMaterialCapture)
        psoDesc.RTVFormats[3] = DXGI_FORMAT_UNKNOWN;
    if (reflectionCapture && !subsurfaceCapture)
        psoDesc.RTVFormats[4] = DXGI_FORMAT_UNKNOWN;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(outPso)));
}

bool CreateSkinnedVariantPipeline(MeshPipelineStore& store, ID3D12Device* device, const VFX::VariantKey& key,
                                  bool wireframe, bool reflectionCapture, bool reflectionMaterialCapture,
                                  bool subsurfaceCapture, ID3D12PipelineState** outPso) {
    D3D12_SHADER_BYTECODE pixelShader{};
    const std::string psId = !key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId;
    const D3D12_SHADER_BYTECODE vertexShader = CookedBytecode(store.skinnedVsShader);
    if (!ResolvePixelShaderBytecode(store, psId, pixelShader, reflectionCapture) ||
        pixelShader.pShaderBytecode == nullptr || vertexShader.pShaderBytecode == nullptr) {
        return false;
    }
    if (!key.vertexShaderId.empty()) {
        DEBUGLOG::PushRenderError(
            std::string(
                "[MeshRenderer][MaterialFx][WARN] Custom vertexShaderId is ignored for skinned mesh. vertexShaderId=") +
            key.vertexShaderId);
    }

    const auto inputElements = GetSkinnedMeshInputLayout();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = store.skinnedRootSig.Get();
    psoDesc.VS = vertexShader;
    psoDesc.PS = pixelShader;
    ApplyCompositeBlendState(psoDesc.BlendState, key.composite, reflectionCapture);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.FrontCounterClockwise = MATH::kFrontFaceCounterClockwise ? TRUE : FALSE;
    psoDesc.RasterizerState.FillMode = wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = key.doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = key.depthTest ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask =
        key.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // capture は 5 出力（色・fallback 鏡面・鏡面応答・材質・次表面散乱の拡散光）。持たない出力は null RTV へ捨てる。
    psoDesc.NumRenderTargets = reflectionCapture ? 5 : 1;
    for (UINT i = 0; i < psoDesc.NumRenderTargets; ++i)
        psoDesc.RTVFormats[i] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (reflectionCapture && !reflectionMaterialCapture)
        psoDesc.RTVFormats[3] = DXGI_FORMAT_UNKNOWN;
    if (reflectionCapture && !subsurfaceCapture)
        psoDesc.RTVFormats[4] = DXGI_FORMAT_UNKNOWN;
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
        GFX::RetireD3D12ObjectForCurrentFrame(retired, debugName != nullptr ? debugName : "MeshRenderer.VariantPSO");
    }
    cache.clear();
}

} // namespace

void ApplyCompositeBlendState(D3D12_BLEND_DESC& blendState, VFX::CompositeMode composite, bool reflectionCapture) {
    blendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    // 無効値の alpha=0 でも奥の反射分量を必ず上書きする。
    blendState.IndependentBlendEnable = reflectionCapture;

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
    } else if (composite == VFX::CompositeMode::Multiply) {
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

void InvalidateMeshPipelineVariants(MeshPipelineStore& store) {
    RetirePipelineCache(store.variantPsoCache, "MeshRenderer.StaticVariantPSO");
    RetirePipelineCache(store.skinnedVariantPsoCache, "MeshRenderer.SkinnedVariantPSO");
    RetirePipelineCache(store.wireVariantPsoCache, "MeshRenderer.WireVariantPSO");
    RetirePipelineCache(store.skinnedWireVariantPsoCache, "MeshRenderer.SkinnedWireVariantPSO");
}

bool AreTraditionalMeshPipelineInputsReady(const MeshPipelineStore& store) {
    return store.rootSig && store.skinnedRootSig && store.vsShader && store.skinnedVsShader && store.psShader &&
           store.depthPso && store.depthSkinnedPso;
}

ID3D12PipelineState* GetOrCreateFallbackPso(MeshPipelineStore& store, ID3D12Device* device, bool skinned) {
    auto& pipeline = skinned ? store.skinnedPso : store.pso;
    if (pipeline || store.fallbackAttempted[skinned] || !device || !AreTraditionalMeshPipelineInputsReady(store))
        return pipeline.Get();
    store.fallbackAttempted[skinned] = true;
    auto desc = store.fallbackDesc;
    if (skinned) {
        const auto layout = GetSkinnedMeshInputLayout();
        desc.pRootSignature = store.skinnedRootSig.Get();
        desc.VS = CookedBytecode(store.skinnedVsShader);
        desc.InputLayout = {layout.data(), static_cast<UINT>(layout.size())};
    }
    CORE::StartupTimer timer(skinned ? "PSO.TraditionalSkinnedFallback" : "PSO.TraditionalStaticFallback");
    (void)HIKARI_DX_CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pipeline.GetAddressOf())),
                          "MeshRenderer::CreateFallbackPipeline");
    return pipeline.Get();
}

ID3D12RootSignature* GetStaticRootSignature(MeshPipelineStore& store) { return store.rootSig.Get(); }

ID3D12RootSignature* GetSkinnedRootSignature(MeshPipelineStore& store) { return store.skinnedRootSig.Get(); }

ID3D12PipelineState* GetOrCreateVariantPso(MeshPipelineStore& store, ID3D12Device* device,
                                           MeshRendererDebugStats& stats, const VFX::VariantKey& key, bool skinned,
                                           bool wireframe, bool reflectionCapture, bool reflectionMaterialCapture,
                                           bool subsurfaceCapture) {
    if ((reflectionMaterialCapture || subsurfaceCapture) && !reflectionCapture)
        return nullptr;
    if (reflectionCapture &&
        (!device || wireframe ||
         (key.composite == VFX::CompositeMode::Additive || key.composite == VFX::CompositeMode::Multiply) ||
         !key.depthTest || !key.depthWrite))
        return nullptr;
    if (reflectionCapture && !store.reflectionShadersAttempted) {
        store.reflectionShadersAttempted = true;
        const auto caps = GFX::QueryShaderDeviceCapabilities(device);
        const auto contract = ASSETS::SHADER::MakeRenderGeometryShaderContract(
            ASSETS::SHADER::RenderGeometryRootLayout::MeshGraphics,
            {caps.shaderModel, caps.rootSignatureVersion, caps.featureMask});
        auto base = ASSETS::SHADER::ResolveBuiltinRenderGeometryShaderEntry(
            "HIKARI.EngineShader.Render3D_StaticReflectionPS", {"Main", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0"},
            contract);
        auto fx = ASSETS::SHADER::ResolveBuiltinRenderGeometryShaderEntry(
            "HIKARI.EngineShader.Render3D_StaticFxReflectionPS", {"Main", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0"},
            contract);
        if (!base || !fx)
            DEBUGLOG::PushRenderError(
                "[MeshRenderer][SSR] Reflection capture shaders unavailable; environment fallback retained.");
        if (base)
            store.reflectionPsShader = std::move(base.lease);
        if (fx)
            store.reflectionFxPsShader = std::move(fx.lease);
    }
    auto& cache = skinned ? (wireframe ? store.skinnedWireVariantPsoCache : store.skinnedVariantPsoCache)
                          : (wireframe ? store.wireVariantPsoCache : store.variantPsoCache);

    const MeshPipelineVariantQuery cacheKey{key, reflectionCapture, reflectionMaterialCapture, subsurfaceCapture};
    auto found = cache.find(cacheKey);
    if (found == cache.end()) {
        ++stats.psoCacheMissCount;
        ComPtr<ID3D12PipelineState> variantPso;
        const bool created =
            skinned
                ? CreateSkinnedVariantPipeline(store, device, key, wireframe, reflectionCapture,
                                               reflectionMaterialCapture, subsurfaceCapture, variantPso.GetAddressOf())
                : CreateVariantPipeline(store, device, key, wireframe, reflectionCapture, reflectionMaterialCapture,
                                        subsurfaceCapture, variantPso.GetAddressOf());
        if (!created && !reflectionCapture) {
            DEBUGLOG::PushRenderError(
                std::string("[MeshRenderer][MaterialFx][WARN] Variant PSO create failed; fallback used. vs=") +
                (key.vertexShaderId.empty() ? "<default>" : key.vertexShaderId) + " ps=" +
                ((!key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId).empty()
                     ? "<default>"
                     : (!key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId)) +
                " skinned=" + (skinned ? "yes" : "no") + " wireframe=" + (wireframe ? "yes" : "no"));
            variantPso = GetOrCreateFallbackPso(store, device, skinned);
        }
        found = cache
                    .emplace(MeshPipelineVariantKey{key, reflectionCapture, reflectionMaterialCapture},
                             std::move(variantPso))
                    .first;
    }

    return found->second.Get();
}

} // namespace HIKARI::MESHRENDERER
