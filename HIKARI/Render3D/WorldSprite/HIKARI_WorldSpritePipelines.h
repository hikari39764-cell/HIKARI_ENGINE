#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <d3d12.h>
#include <wrl/client.h>

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
    Count
};
using WorldSpritePipelineSet =
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(WorldSpritePipeline::Count)>;
// Root layout と MS/VS alpha variants の生成を一か所で所有する。
bool CreateWorldSpritePipelines(ID3D12Device* device, bool preferRaster,
                                Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature,
                                WorldSpritePipelineSet& pipelines, bool& meshPipeline);
} // namespace HIKARI::RENDER3D::WORLD_SPRITES
