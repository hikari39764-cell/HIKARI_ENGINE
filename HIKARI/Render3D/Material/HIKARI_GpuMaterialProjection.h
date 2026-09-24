#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"

namespace HIKARI {
struct MaterialAsset;
enum class AlphaMode;
} // namespace HIKARI

namespace HIKARI::RENDER3D::MATERIAL {

enum class GpuMaterialTextureChannel : uint8_t {
    BaseColor,
    Normal,
    Emissive,
    MetallicRoughness,
    Occlusion,
    Specular,
    SpecularColor,
    Count,
};

inline constexpr std::array<MaterialTextureUsage, static_cast<size_t>(GpuMaterialTextureChannel::Count)>
    kGpuMaterialTextureUsages{MaterialTextureUsage::BaseColor,    MaterialTextureUsage::Normal,
                              MaterialTextureUsage::Emissive,     MaterialTextureUsage::MetallicRoughness,
                              MaterialTextureUsage::Occlusion,    MaterialTextureUsage::Specular,
                              MaterialTextureUsage::SpecularColor};

struct GpuMaterialTextureProjection {
    int32_t backendHandle = -1;
    uint32_t descriptorIndex = kInvalidTextureDescriptorIndex;
    MATH::Vec4 uvTransform{1.0f, 1.0f, 0.0f, 0.0f};
    float uvRotation = 0.0f;
    uint32_t uvSet = 0u;
    bool enabled = false;
};

// CPU authoring の共通係数。係数だけを読む経路では texture 配列を作らない。
struct MaterialFactorProjection {
    MATH::Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    MATH::Vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 1.0f};
    MATH::Vec4 pbrParams{0.0f, 1.0f, 1.0f, 0.5f};
    MATH::Vec4 specularParams{1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t materialFlags = 0u;
    float normalScale = 1.0f;
};

// Material-owned projection into the shared C++/HLSL layout.
struct GpuMaterialProjection : MaterialFactorProjection {
    GpuMaterialProjection() = default;
    explicit GpuMaterialProjection(const MaterialFactorProjection& factors) : MaterialFactorProjection(factors) {}
    std::array<GpuMaterialTextureProjection, static_cast<size_t>(GpuMaterialTextureChannel::Count)> textures{};
};

// モデル既定値と材質オーバーライドの共通解決。テクスチャのロードは行わない。
AlphaMode ResolveMaterialAlphaMode(const MaterialAsset* modelMaterial, const Material* runtimeMaterial);
MaterialFactorProjection BuildMaterialFactorProjection(const MaterialAsset* modelMaterial,
                                                       const Material* runtimeMaterial);

GpuMaterialProjection BuildRuntimeMaterialGpuProjection(const Material& material);

// テクスチャの所有権は変更せず、継承規則に従う UV だけを射影する。
void ProjectMaterialTextureUv(const MaterialAsset* modelMaterial, const Material* runtimeMaterial,
                              MaterialTextureUsage usage, GpuMaterialTextureProjection& outTexture);

GpuMaterialData PackGpuMaterialData(const GpuMaterialProjection& projection);

std::vector<uint32_t> ResolveMaterialProgramResourceDescriptorIndices(const Material& material);

} // namespace HIKARI::RENDER3D::MATERIAL
