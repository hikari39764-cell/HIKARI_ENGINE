#pragma once

#include <cstdint>
#include <string>

namespace HIKARI {

class Material;
struct MaterialAsset;
struct TextureAsset3D;
struct TextureSlot;
struct RuntimeTextureSlot;

namespace RENDER3D {
enum class TextureResourceColorSpace : uint8_t;
}

enum class MaterialTextureUsage {
    BaseColor,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
    Specular,
    SpecularColor,
    // 筆触の殻（Painterly Shell）。実行時材質だけが持ち、モデルの材質に対応する slot は無い。
    StrokeMask,
    StrokeNormal,
    Unknown,
};

const char* MaterialTextureUsageName(MaterialTextureUsage usage);

RENDER3D::TextureResourceColorSpace MaterialTextureColorSpace(MaterialTextureUsage usage);

const TextureSlot* GetModelMaterialTextureSlot(const MaterialAsset* material, MaterialTextureUsage usage);
// nullptr はモデル継承。無効 slot の非 nullptr は、継承を明示的に止める。
const RuntimeTextureSlot* ResolveMaterialTextureOverride(const Material* material, MaterialTextureUsage usage);
const std::string& GetMaterialTextureRuntimePath(const TextureAsset3D& texture);
const std::string& GetMaterialTextureRuntimePath(const RuntimeTextureSlot& texture);

} // namespace HIKARI
