#pragma once

#include <array>
#include <memory>
#include <span>

#include "Assets/Shader/HIKARI_ShaderCookedArtifact.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpritePipelines.h"

namespace HIKARI::RENDER3D::WORLD_SPRITES {

// 描画項目は不変 lease を保持し、cache eviction でも PSO の参照を失わない。
struct WorldSpriteMaterialPipelines {
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 3u> states{};
    ID3D12PipelineState* Get(WorldSpritePipeline kind) const noexcept;
};

class WorldSpriteMaterials final {
public:
    WorldSpriteMaterials();
    ~WorldSpriteMaterials();
    void BeginFrame(const GFX::Context& context, uint64_t registryId, uint64_t registryRevision);
    void Shutdown() noexcept;
    std::shared_ptr<const WorldSpriteMaterialPipelines> Resolve(
        const std::shared_ptr<const ASSETS::SHADER::ShaderCookedArtifact>& artifact, ID3D12RootSignature* root,
        bool meshPipeline, std::span<const uint8_t> geometry);
    bool UploadParameters(std::span<const MATH::Vec4> values, uint64_t& uploadedBytes);
    D3D12_GPU_VIRTUAL_ADDRESS GetParameterAddress(D3D12_GPU_VIRTUAL_ADDRESS fallback) const noexcept;
    uint64_t GetResidentBytes() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace HIKARI::RENDER3D::WORLD_SPRITES
