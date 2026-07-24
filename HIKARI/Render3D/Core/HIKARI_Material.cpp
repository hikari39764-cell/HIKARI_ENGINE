#include "Render3D/Core/HIKARI_Material.h"

#include <algorithm>
#include <utility>

namespace HIKARI {
    namespace {
        RuntimeTextureSlot kEmptyTextureSlot{};

        bool Equal(const MATH::Vec2& lhs, const MATH::Vec2& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y;
        }

        bool Equal(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        bool Equal(const MATH::Vec4& lhs, const MATH::Vec4& rhs) {
            return
                lhs.x == rhs.x && lhs.y == rhs.y &&
                lhs.z == rhs.z && lhs.w == rhs.w;
        }

        bool Equal(const RuntimeTextureSlot& lhs, const RuntimeTextureSlot& rhs) {
            return
                lhs.sourcePath == rhs.sourcePath &&
                lhs.resolvedPath == rhs.resolvedPath &&
                lhs.resource == rhs.resource &&
                lhs.handle == rhs.handle &&
                lhs.enabled == rhs.enabled &&
                lhs.texCoord == rhs.texCoord &&
                Equal(lhs.uvScale, rhs.uvScale) &&
                Equal(lhs.uvOffset, rhs.uvOffset) &&
                lhs.uvRotation == rhs.uvRotation;
        }
    }

    void Material::SetBaseColor(const MATH::Vec4& color) {
        if (Equal(baseColor_, color)) {
            return;
        }
        baseColor_ = color;
        ++revision_;
    }

    const MATH::Vec4& Material::GetBaseColor() const {
        return baseColor_;
    }

    void Material::SetBaseColorTexturePath(std::string path) {
        RuntimeTextureSlot next = baseColorTexture_;
        next.resolvedPath = std::move(path);
        if (next.sourcePath.empty()) {
            next.sourcePath = next.resolvedPath;
        }
        next.enabled = !next.resolvedPath.empty();
        if (Equal(baseColorTexture_, next)) {
            return;
        }
        baseColorTexture_ = std::move(next);
        ++revision_;
    }

    const std::string& Material::GetBaseColorTexturePath() const {
        return baseColorTexture_.resolvedPath;
    }

    void Material::SetBaseColorTextureHandle(int handle) {
        RuntimeTextureSlot next = baseColorTexture_;
        next.resource = {};
        next.handle = handle;
        next.enabled = handle >= 0;
        if (Equal(baseColorTexture_, next)) {
            return;
        }
        baseColorTexture_ = std::move(next);
        ++revision_;
    }

    int Material::GetBaseColorTextureHandle() const {
        return baseColorTexture_.handle;
    }

    bool Material::HasBaseColorTexture() const {
        return baseColorTexture_.IsActive();
    }

    void Material::SetShaderProfileId(std::string shaderProfileId) {
        if (shaderProfileId_ == shaderProfileId) {
            return;
        }
        shaderProfileId_ = std::move(shaderProfileId);
        ++revision_;
    }

    const std::string& Material::GetShaderProfileId() const {
        return shaderProfileId_;
    }

    void Material::SetFeatureBits(uint32_t featureBits) {
        if (featureBits_ == featureBits) {
            return;
        }
        featureBits_ = featureBits;
        ++revision_;
    }

    uint32_t Material::GetFeatureBits() const {
        return featureBits_;
    }

    void Material::SetTextureSlot(MaterialTextureUsage usage, RuntimeTextureSlot slot) {
        RuntimeTextureSlot* destination = nullptr;
        switch (usage) {
        case MaterialTextureUsage::BaseColor:
            destination = &baseColorTexture_;
            break;
        case MaterialTextureUsage::Normal:
            destination = &normalTexture_;
            break;
        case MaterialTextureUsage::MetallicRoughness:
            destination = &metallicRoughnessTexture_;
            break;
        case MaterialTextureUsage::Occlusion:
            destination = &occlusionTexture_;
            break;
        case MaterialTextureUsage::Emissive:
            destination = &emissiveTexture_;
            break;
        case MaterialTextureUsage::Specular:
            destination = &specularTexture_;
            break;
        case MaterialTextureUsage::SpecularColor:
            destination = &specularColorTexture_;
            break;
        default:
            return;
        }
        if (Equal(*destination, slot)) {
            return;
        }
        *destination = std::move(slot);
        ++revision_;
    }

    const RuntimeTextureSlot& Material::GetTextureSlot(MaterialTextureUsage usage) const {
        switch (usage) {
        case MaterialTextureUsage::BaseColor:
            return baseColorTexture_;
        case MaterialTextureUsage::Normal:
            return normalTexture_;
        case MaterialTextureUsage::MetallicRoughness:
            return metallicRoughnessTexture_;
        case MaterialTextureUsage::Occlusion:
            return occlusionTexture_;
        case MaterialTextureUsage::Emissive:
            return emissiveTexture_;
        case MaterialTextureUsage::Specular:
            return specularTexture_;
        case MaterialTextureUsage::SpecularColor:
            return specularColorTexture_;
        default:
            return kEmptyTextureSlot;
        }
    }

    bool Material::HasTextureSlot(MaterialTextureUsage usage) const {
        return GetTextureSlot(usage).IsActive();
    }

    void Material::SetMetallicFactor(float value) {
        const float next = std::clamp(value, 0.0f, 1.0f);
        if (metallicFactor_ == next) {
            return;
        }
        metallicFactor_ = next;
        ++revision_;
    }

    float Material::GetMetallicFactor() const {
        return metallicFactor_;
    }

    void Material::SetRoughnessFactor(float value) {
        const float next = std::clamp(value, 0.04f, 1.0f);
        if (roughnessFactor_ == next) {
            return;
        }
        roughnessFactor_ = next;
        ++revision_;
    }

    float Material::GetRoughnessFactor() const {
        return roughnessFactor_;
    }

    void Material::SetSpecularFactor(float value) {
        const float next = std::max(0.0f, value);
        if (specularFactor_ == next) {
            return;
        }
        specularFactor_ = next;
        ++revision_;
    }

    float Material::GetSpecularFactor() const {
        return specularFactor_;
    }

    void Material::SetSpecularColorFactor(const MATH::Vec3& value) {
        if (Equal(specularColorFactor_, value)) {
            return;
        }
        specularColorFactor_ = value;
        ++revision_;
    }

    const MATH::Vec3& Material::GetSpecularColorFactor() const {
        return specularColorFactor_;
    }

    void Material::SetNormalScale(float value) {
        if (normalScale_ == value) {
            return;
        }
        normalScale_ = value;
        ++revision_;
    }

    float Material::GetNormalScale() const {
        return normalScale_;
    }

    void Material::SetOcclusionStrength(float value) {
        if (occlusionStrength_ == value) {
            return;
        }
        occlusionStrength_ = value;
        ++revision_;
    }

    float Material::GetOcclusionStrength() const {
        return occlusionStrength_;
    }

    void Material::SetEmissiveFactor(const MATH::Vec3& value) {
        if (Equal(emissiveFactor_, value)) {
            return;
        }
        emissiveFactor_ = value;
        ++revision_;
    }

    const MATH::Vec3& Material::GetEmissiveFactor() const {
        return emissiveFactor_;
    }

    void Material::SetEmissiveStrength(float value) {
        if (emissiveStrength_ == value) {
            return;
        }
        emissiveStrength_ = value;
        ++revision_;
    }

    float Material::GetEmissiveStrength() const {
        return emissiveStrength_;
    }

    uint64_t Material::GetRevision() const {
        return revision_;
    }

} // namespace HIKARI
