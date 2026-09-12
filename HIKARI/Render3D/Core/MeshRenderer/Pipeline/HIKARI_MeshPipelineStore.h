#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include <d3d12.h>
#include <wrl/client.h>

#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::MESHRENDERER {

    struct MeshPipelineVariantKey {
        VFX::VariantKey material;
        bool reflectionCapture = false;
        bool operator==(const MeshPipelineVariantKey&) const = default;
    };

    struct MeshPipelineVariantQuery {
        const VFX::VariantKey& material;
        bool reflectionCapture = false;
    };
    struct VariantKeyHasher {
        using is_transparent = void;
        size_t operator()(const MeshPipelineVariantQuery& key) const noexcept;
        size_t operator()(const MeshPipelineVariantKey& key) const noexcept {
            return operator()(MeshPipelineVariantQuery{key.material, key.reflectionCapture});
        }
    };
    struct VariantKeyEqual {
        using is_transparent = void;
        template<class L, class R> bool operator()(const L& left, const R& right) const noexcept {
            return left.reflectionCapture == right.reflectionCapture && left.material == right.material;
        }
    };

    struct MeshPipelineStore {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> skinnedRootSig;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> skinnedPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> depthPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> depthSkinnedPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> waterDepthPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> waterDepthSkinnedPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> geometryPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> geometrySkinnedPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> geometryFeatureMaskPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> geometryFeatureMaskSkinnedPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> geometryOutlinePso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> geometryOutlineSkinnedPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> visibilityOverlayPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> visibilityOverlaySkinnedPso;
        ASSETS::SHADER::ShaderAssetEntryLease vsShader{};
        ASSETS::SHADER::ShaderAssetEntryLease skinnedVsShader{};
        ASSETS::SHADER::ShaderAssetEntryLease psShader{};
        ASSETS::SHADER::ShaderAssetEntryLease materialFxPsShader{};
        ASSETS::SHADER::ShaderAssetEntryLease reflectionPsShader{};
        ASSETS::SHADER::ShaderAssetEntryLease reflectionFxPsShader{};
        bool reflectionShadersAttempted = false;
        ASSETS::SHADER::ShaderAssetEntryLease waterDepthPsShader{};
        ASSETS::SHADER::ShaderAssetEntryLease geometryPsShader{};
        ASSETS::SHADER::ShaderAssetEntryLease geometryFeatureMaskPsShader{};
        ASSETS::SHADER::ShaderAssetEntryLease geometryOutlineVsShader{};
        ASSETS::SHADER::ShaderAssetEntryLease geometryOutlineSkinnedVsShader{};
        ASSETS::SHADER::ShaderAssetEntryLease geometryOutlinePsShader{};
        ASSETS::SHADER::ShaderAssetEntryLease visibilityOverlayPsShader{};
        std::unordered_map<MeshPipelineVariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher, VariantKeyEqual> variantPsoCache;
        std::unordered_map<MeshPipelineVariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher, VariantKeyEqual> skinnedVariantPsoCache;
        std::unordered_map<MeshPipelineVariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher, VariantKeyEqual> wireVariantPsoCache;
        std::unordered_map<MeshPipelineVariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher, VariantKeyEqual> skinnedWireVariantPsoCache;
    };

    bool InitializeMeshPipelines(ID3D12Device* device, MeshPipelineStore& store);
    void ShutdownMeshPipelines(MeshPipelineStore& store);
    void InvalidateMeshPipelineVariants(MeshPipelineStore& store);

    ID3D12RootSignature* GetStaticRootSignature(MeshPipelineStore& store);
    ID3D12RootSignature* GetSkinnedRootSignature(MeshPipelineStore& store);
    ID3D12PipelineState* GetDepthPso(MeshPipelineStore& store, bool skinned);
    ID3D12PipelineState* GetWaterDepthPso(MeshPipelineStore& store, bool skinned);
    ID3D12PipelineState* GetGeometryPso(
        MeshPipelineStore& store,
        bool skinned,
        bool featureMask = false);
    ID3D12PipelineState* GetGeometryOutlinePso(
        MeshPipelineStore& store,
        bool skinned);
    ID3D12PipelineState* GetVisibilityOverlayPso(
        MeshPipelineStore& store,
        bool skinned);

    ID3D12PipelineState* GetOrCreateVariantPso(
        MeshPipelineStore& store,
        ID3D12Device* device,
        MeshRendererDebugStats& stats,
        const VFX::VariantKey& key,
        bool skinned,
        bool wireframe,
        bool reflectionCapture = false);

    void ApplyCompositeBlendState(D3D12_BLEND_DESC& blendState, VFX::CompositeMode composite,
        bool reflectionCapture = false);

} // namespace HIKARI::MESHRENDERER
