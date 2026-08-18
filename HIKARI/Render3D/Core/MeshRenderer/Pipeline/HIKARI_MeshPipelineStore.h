#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::MESHRENDERER {

    struct VariantKeyHasher {
        size_t operator()(const VFX::VariantKey& key) const noexcept;
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
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> skinnedVsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> waterDepthPsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> geometryPsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> geometryFeatureMaskPsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> geometryOutlineVsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> geometryOutlineSkinnedVsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> geometryOutlinePsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> visibilityOverlayPsBlob;
        std::unordered_map<VFX::VariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher> variantPsoCache;
        std::unordered_map<VFX::VariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher> skinnedVariantPsoCache;
        std::unordered_map<VFX::VariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher> wireVariantPsoCache;
        std::unordered_map<VFX::VariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher> skinnedWireVariantPsoCache;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> psBlobCache;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> vsBlobCache;
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
        bool wireframe);

    void ApplyCompositeBlendState(D3D12_BLEND_DESC& blendState, VFX::CompositeMode composite);

} // namespace HIKARI::MESHRENDERER
