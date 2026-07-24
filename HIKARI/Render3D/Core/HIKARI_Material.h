#pragma once
#include <string>
#include <cstdint>
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI {

    struct RuntimeTextureSlot {
        std::string sourcePath{};
        std::string resolvedPath{};
        RENDER3D::TextureResourceHandle resource{};
        int handle = -1;
        bool enabled = false;
        int texCoord = 0;
        MATH::Vec2 uvScale{ 1.0f, 1.0f };
        MATH::Vec2 uvOffset{ 0.0f, 0.0f };
        float uvRotation = 0.0f;

        bool IsValid() const {
            return resource.IsValid() || handle >= 0;
        }

        bool IsActive() const {
            return enabled && IsValid();
        }
    };

    class Material {
    public:
        void SetBaseColor(const MATH::Vec4& color);
        const MATH::Vec4& GetBaseColor() const;
        void SetBaseColorTexturePath(std::string path);
        const std::string& GetBaseColorTexturePath() const;
        void SetBaseColorTextureHandle(int handle);
        int GetBaseColorTextureHandle() const;
        bool HasBaseColorTexture() const;

        void SetShaderProfileId(std::string shaderProfileId);
        const std::string& GetShaderProfileId() const;
        void SetFeatureBits(uint32_t featureBits);
        uint32_t GetFeatureBits() const;

        void SetTextureSlot(MaterialTextureUsage usage, RuntimeTextureSlot slot);
        const RuntimeTextureSlot& GetTextureSlot(MaterialTextureUsage usage) const;
        bool HasTextureSlot(MaterialTextureUsage usage) const;

        void SetMetallicFactor(float value);
        float GetMetallicFactor() const;
        void SetRoughnessFactor(float value);
        float GetRoughnessFactor() const;
        void SetSpecularFactor(float value);
        float GetSpecularFactor() const;
        void SetSpecularColorFactor(const MATH::Vec3& value);
        const MATH::Vec3& GetSpecularColorFactor() const;
        void SetNormalScale(float value);
        float GetNormalScale() const;
        void SetOcclusionStrength(float value);
        float GetOcclusionStrength() const;
        void SetEmissiveFactor(const MATH::Vec3& value);
        const MATH::Vec3& GetEmissiveFactor() const;
        void SetEmissiveStrength(float value);
        float GetEmissiveStrength() const;
        uint64_t GetRevision() const;

    private:
        MATH::Vec4 baseColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
        RuntimeTextureSlot baseColorTexture_{};
        RuntimeTextureSlot normalTexture_{};
        RuntimeTextureSlot metallicRoughnessTexture_{};
        RuntimeTextureSlot occlusionTexture_{};
        RuntimeTextureSlot emissiveTexture_{};
        RuntimeTextureSlot specularTexture_{};
        RuntimeTextureSlot specularColorTexture_{};
        float metallicFactor_ = 0.0f;
        float roughnessFactor_ = 1.0f;
        float specularFactor_ = 1.0f;
        MATH::Vec3 specularColorFactor_{ 1.0f, 1.0f, 1.0f };
        float normalScale_ = 1.0f;
        float occlusionStrength_ = 1.0f;
        MATH::Vec3 emissiveFactor_{ 0.0f, 0.0f, 0.0f };
        float emissiveStrength_ = 1.0f;
        std::string shaderProfileId_{};
        uint32_t featureBits_ = 0;
        uint64_t revision_ = 1;
    };

} // namespace HIKARI
