#include "Render3D/Core/HIKARI_Material.h"

#include <algorithm>
#include <atomic>
#include <utility>
#include "Assets/Models/Policies/HIKARI_ModelMaterialPolicy.h"

#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI {
namespace {
RuntimeTextureSlot kEmptyTextureSlot{};
std::atomic<uint64_t> gNextMaterialRevision{1u};

uint64_t NextMaterialRevision() noexcept { return gNextMaterialRevision.fetch_add(1u, std::memory_order_relaxed); }

bool Equal(const MATH::Vec2& lhs, const MATH::Vec2& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y; }

bool Equal(const MATH::Vec3& lhs, const MATH::Vec3& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z; }

bool Equal(const MATH::Vec4& lhs, const MATH::Vec4& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

bool Equal(const RuntimeTextureSlot& lhs, const RuntimeTextureSlot& rhs) {
    return lhs.sourcePath == rhs.sourcePath && lhs.resolvedPath == rhs.resolvedPath && lhs.resource == rhs.resource &&
           lhs.handle == rhs.handle && lhs.enabled == rhs.enabled && lhs.texCoord == rhs.texCoord &&
           Equal(lhs.uvScale, rhs.uvScale) && Equal(lhs.uvOffset, rhs.uvOffset) && lhs.uvRotation == rhs.uvRotation;
}

bool Equal(const std::vector<RuntimeTextureSlot>& lhs, const std::vector<RuntimeTextureSlot>& rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](const RuntimeTextureSlot& lhsValue, const RuntimeTextureSlot& rhsValue) {
                          return Equal(lhsValue, rhsValue);
                      });
}
} // namespace

Material::Material() : revision_(NextMaterialRevision()) {}

void Material::SetShellSettings(const MaterialShellSettings& settings) {
    if (!IsValidMaterialShellSettings(settings) || shellSettings_ == settings)
        return;
    shellSettings_ = settings;
    TouchRevision();
}

void Material::TouchRevision() noexcept { revision_ = NextMaterialRevision(); }

int RuntimeTextureSlot::ResolveBackendHandle() const {
    if (resource) {
        return RENDER3D::GetTextureResourceBackendHandle(resource);
    }
    return RENDER3D::IsTextureResourceBackendHandleValid(handle) ? handle : -1;
}

bool RuntimeTextureSlot::IsValid() const { return ResolveBackendHandle() >= 0; }

void Material::SetBaseColor(const MATH::Vec4& color) {
    if (Equal(baseColor_, color)) {
        return;
    }
    baseColor_ = color;
    TouchRevision();
}

const MATH::Vec4& Material::GetBaseColor() const { return baseColor_; }

const std::string& Material::GetBaseColorTexturePath() const { return baseColorTexture_.resolvedPath; }

bool Material::HasBaseColorTexture() const { return baseColorTexture_.IsActive(); }

void Material::SetShaderProfileId(std::string shaderProfileId) {
    if (shaderProfileId_ == shaderProfileId) {
        return;
    }
    shaderProfileId_ = std::move(shaderProfileId);
    TouchRevision();
}

const std::string& Material::GetShaderProfileId() const { return shaderProfileId_; }

void Material::SetFeatureBits(uint32_t featureBits) {
    if (featureBits_ == featureBits) {
        return;
    }
    featureBits_ = featureBits;
    TouchRevision();
}

uint32_t Material::GetFeatureBits() const { return featureBits_; }

void Material::SetMaterialProgram(RENDER3D::MATERIAL::MaterialProgramId programId) {
    if (materialProgramId_ == programId) {
        return;
    }
    materialProgramId_ = programId;
    TouchRevision();
}

RENDER3D::MATERIAL::MaterialProgramId Material::GetMaterialProgramId() const { return materialProgramId_; }

void Material::SetMaterialProgramResourceBlock(std::vector<RuntimeTextureSlot> values) {
    if (Equal(materialProgramResourceBlock_, values)) {
        return;
    }
    materialProgramResourceBlock_ = std::move(values);
    TouchRevision();
}

std::span<const RuntimeTextureSlot> Material::GetMaterialProgramResourceBlock() const {
    return materialProgramResourceBlock_;
}

void Material::SetMaterialProgramInstanceFlags(uint32_t flags) {
    if (materialProgramInstanceFlags_ == flags) {
        return;
    }
    materialProgramInstanceFlags_ = flags;
    TouchRevision();
}

uint32_t Material::GetMaterialProgramInstanceFlags() const { return materialProgramInstanceFlags_; }

void Material::SetMaterialAlphaMode(RuntimeMaterialAlphaMode alphaMode) {
    if (materialAlphaMode_ == alphaMode) {
        return;
    }
    materialAlphaMode_ = alphaMode;
    TouchRevision();
}

RuntimeMaterialAlphaMode Material::GetMaterialAlphaMode() const { return materialAlphaMode_; }

void Material::SetAlphaCutoff(float alphaCutoff) {
    const float next = std::clamp(alphaCutoff, 0.0f, 1.0f);
    if (alphaCutoff_ == next) {
        return;
    }
    alphaCutoff_ = next;
    TouchRevision();
}

float Material::GetAlphaCutoff() const { return alphaCutoff_; }

void Material::SetDoubleSided(bool doubleSided) {
    if (doubleSided_ == doubleSided) {
        return;
    }
    doubleSided_ = doubleSided;
    TouchRevision();
}

bool Material::IsDoubleSided() const { return doubleSided_; }

void Material::SetUnlit(bool unlit) {
    SetFeatureBits(unlit ? featureBits_ | MATERIAL_FEATURES::Unlit : featureBits_ & ~MATERIAL_FEATURES::Unlit);
}

void Material::SetTextureSlot(MaterialTextureUsage usage, RuntimeTextureSlot slot) {
    RuntimeTextureSlot* destination = nullptr;
    switch (usage) {
    case MaterialTextureUsage::BaseColor: destination = &baseColorTexture_; break;
    case MaterialTextureUsage::Normal: destination = &normalTexture_; break;
    case MaterialTextureUsage::MetallicRoughness: destination = &metallicRoughnessTexture_; break;
    case MaterialTextureUsage::Occlusion: destination = &occlusionTexture_; break;
    case MaterialTextureUsage::Emissive: destination = &emissiveTexture_; break;
    case MaterialTextureUsage::Specular: destination = &specularTexture_; break;
    case MaterialTextureUsage::SpecularColor: destination = &specularColorTexture_; break;
    case MaterialTextureUsage::StrokeMask: destination = &strokeMaskTexture_; break;
    case MaterialTextureUsage::StrokeNormal: destination = &strokeNormalTexture_; break;
    default: return;
    }
    if (Equal(*destination, slot)) {
        return;
    }
    *destination = std::move(slot);
    TouchRevision();
}

const RuntimeTextureSlot& Material::GetTextureSlot(MaterialTextureUsage usage) const {
    switch (usage) {
    case MaterialTextureUsage::BaseColor: return baseColorTexture_;
    case MaterialTextureUsage::Normal: return normalTexture_;
    case MaterialTextureUsage::MetallicRoughness: return metallicRoughnessTexture_;
    case MaterialTextureUsage::Occlusion: return occlusionTexture_;
    case MaterialTextureUsage::Emissive: return emissiveTexture_;
    case MaterialTextureUsage::Specular: return specularTexture_;
    case MaterialTextureUsage::SpecularColor: return specularColorTexture_;
    case MaterialTextureUsage::StrokeMask: return strokeMaskTexture_;
    case MaterialTextureUsage::StrokeNormal: return strokeNormalTexture_;
    default: return kEmptyTextureSlot;
    }
}

bool Material::HasTextureSlot(MaterialTextureUsage usage) const { return GetTextureSlot(usage).IsActive(); }

void Material::SetInheritModelTextures(bool inherit) {
    if (inheritModelTextures_ == inherit) {
        return;
    }
    inheritModelTextures_ = inherit;
    TouchRevision();
}

bool Material::InheritsModelTextures() const { return inheritModelTextures_; }

void Material::SetMetallicFactor(float value) {
    const float next = std::clamp(value, 0.0f, 1.0f);
    if (metallicFactor_ == next) {
        return;
    }
    metallicFactor_ = next;
    TouchRevision();
}

float Material::GetMetallicFactor() const { return metallicFactor_; }

void Material::SetRoughnessFactor(float value) {
    const float next = std::clamp(value, 0.04f, 1.0f);
    if (roughnessFactor_ == next) {
        return;
    }
    roughnessFactor_ = next;
    TouchRevision();
}

float Material::GetRoughnessFactor() const { return roughnessFactor_; }

void Material::SetSpecularFactor(float value) {
    const float next = (std::max)(0.0f, value);
    if (specularFactor_ == next) {
        return;
    }
    specularFactor_ = next;
    TouchRevision();
}

float Material::GetSpecularFactor() const { return specularFactor_; }

void Material::SetSpecularColorFactor(const MATH::Vec3& value) {
    if (Equal(specularColorFactor_, value)) {
        return;
    }
    specularColorFactor_ = value;
    TouchRevision();
}

const MATH::Vec3& Material::GetSpecularColorFactor() const { return specularColorFactor_; }

void Material::SetSurfaceLayers(const MATH::Vec4& value) {
    const MATH::Vec4 next{std::clamp(value.x, 0.0f, 1.0f), std::clamp(value.y, 0.0f, 1.0f),
                          std::clamp(value.z, 0.0f, 1.0f), std::clamp(value.w, 1.0f, 3.0f)};
    if (Equal(surfaceLayers_, next)) {
        return;
    }
    surfaceLayers_ = next;
    TouchRevision();
}

void Material::SetSubsurface(const MATH::Vec2& value) {
    const MATH::Vec2 next{std::clamp(value.x, 0.0f, 1.0f), std::clamp(value.y, 0.0f, 1.0f)};
    if (Equal(subsurface_, next)) {
        return;
    }
    subsurface_ = next;
    TouchRevision();
}

void Material::SetNormalScale(float value) {
    if (normalScale_ == value) {
        return;
    }
    normalScale_ = value;
    TouchRevision();
}

float Material::GetNormalScale() const { return normalScale_; }

void Material::SetOcclusionStrength(float value) {
    if (occlusionStrength_ == value) {
        return;
    }
    occlusionStrength_ = value;
    TouchRevision();
}

float Material::GetOcclusionStrength() const { return occlusionStrength_; }

void Material::SetEmissiveFactor(const MATH::Vec3& value) {
    if (Equal(emissiveFactor_, value)) {
        return;
    }
    emissiveFactor_ = value;
    TouchRevision();
}

const MATH::Vec3& Material::GetEmissiveFactor() const { return emissiveFactor_; }

void Material::SetEmissiveStrength(float value) {
    if (emissiveStrength_ == value) {
        return;
    }
    emissiveStrength_ = value;
    TouchRevision();
}

float Material::GetEmissiveStrength() const { return emissiveStrength_; }

uint64_t Material::GetRevision() const { return revision_; }

} // namespace HIKARI
