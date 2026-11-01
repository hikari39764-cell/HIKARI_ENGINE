#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <span>
#include <d3d12.h>
#include <wrl/client.h>

#include "RenderContracts/Shader/HIKARI_ShaderAssetResolver.h"

namespace HIKARI::RENDER3D::WORLD_SPRITES {
enum class WorldSpritePipeline : uint32_t {
    Opaque,
    Mask,
    Blend,
    HybridCore,
    Depth,
    Shadow,
    Surface,
    SurfaceFeature,
    DepthOfFieldCapture,
    Count
};
using WorldSpritePipelineSet =
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(WorldSpritePipeline::Count)>;
// Root layout と MS/VS alpha variants の生成を一か所で所有する。
bool CreateWorldSpritePipelines(ID3D12Device* device, bool preferRaster,
                                Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature,
                                WorldSpritePipelineSet& pipelines, bool& meshPipeline,
                                ASSETS::SHADER::ShaderAssetEntryLease& outGeometryShader);
bool CreateWorldSpriteProjectPipeline(ID3D12Device* device, bool meshPipeline, ID3D12RootSignature* rootSignature,
                                      WorldSpritePipeline kind, std::span<const uint8_t> geometryShaderDxil,
                                      std::span<const uint8_t> pixelShaderDxil,
                                      Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline);
} // namespace HIKARI::RENDER3D::WORLD_SPRITES
