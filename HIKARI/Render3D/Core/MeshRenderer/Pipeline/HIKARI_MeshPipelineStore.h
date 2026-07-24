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
        Microsoft::WRL::ComPtr<ID3D12PipelineState> geometryPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> geometrySkinnedPso;
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> skinnedVsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> geometryPsBlob;
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
    ID3D12PipelineState* GetGeometryPso(MeshPipelineStore& store, bool skinned);

    ID3D12PipelineState* GetOrCreateVariantPso(
        MeshPipelineStore& store,
        ID3D12Device* device,
        MeshRendererDebugStats& stats,
        const VFX::VariantKey& key,
        bool skinned,
        bool wireframe);

    void ApplyCompositeBlendState(D3D12_BLEND_DESC& blendState, VFX::CompositeMode composite);

} // namespace HIKARI::MESHRENDERER
