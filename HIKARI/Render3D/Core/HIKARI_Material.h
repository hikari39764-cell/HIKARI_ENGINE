#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Assets/Material/HIKARI_MaterialShellSettings.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramTypes.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI {

namespace RENDER3D::MATERIAL {
struct MaterialParameterProjectionRecipe;
}

enum class RuntimeMaterialAlphaMode : uint8_t {
    Opaque,
    Mask,
    Blend,
};

struct RuntimeTextureSlot {
    std::string sourcePath{};
    std::string resolvedPath{};
    RENDER3D::TextureResourceHandle resource{};
    int handle = -1;
    bool enabled = false;
    int texCoord = 0;
    MATH::Vec2 uvScale{1.0f, 1.0f};
    MATH::Vec2 uvOffset{0.0f, 0.0f};
    float uvRotation = 0.0f;

    int ResolveBackendHandle() const;
    bool IsValid() const;

    bool IsActive() const { return enabled && IsValid(); }
};

class Material {
public:
    Material();

    void SetBaseColor(const MATH::Vec4& color);
    const MATH::Vec4& GetBaseColor() const;
    const std::string& GetBaseColorTexturePath() const;
    bool HasBaseColorTexture() const;

    void SetShaderProfileId(std::string shaderProfileId);
    const std::string& GetShaderProfileId() const;
    void SetFeatureBits(uint32_t featureBits);
    uint32_t GetFeatureBits() const;

    void SetMaterialProgram(RENDER3D::MATERIAL::MaterialProgramId programId);
    RENDER3D::MATERIAL::MaterialProgramId GetMaterialProgramId() const;
    void SetMaterialProgramParameterBlock(std::vector<MATH::Vec4> values);
    std::span<const MATH::Vec4> GetMaterialProgramParameterBlock() const;
    void SetMaterialParameterProjection(
        std::shared_ptr<const RENDER3D::MATERIAL::MaterialParameterProjectionRecipe> recipe);
    const std::shared_ptr<const RENDER3D::MATERIAL::MaterialParameterProjectionRecipe>& GetMaterialParameterProjection()
        const noexcept;
    bool NeedsMaterialParameterProjection(uint64_t sourceRevision) const noexcept;
    bool NeedsMaterialParameterProjection(uint64_t sourceInstanceId, uint64_t sourceRevision) const noexcept;
    void ApplyMaterialParameterProjection(std::vector<MATH::Vec4> values, uint64_t sourceRevision);
    void ApplyMaterialParameterProjection(std::vector<MATH::Vec4> values, uint64_t sourceInstanceId,
                                          uint64_t sourceRevision);
    void SetMaterialProgramResourceBlock(std::vector<RuntimeTextureSlot> values);
    std::span<const RuntimeTextureSlot> GetMaterialProgramResourceBlock() const;
    void SetMaterialProgramInstanceFlags(uint32_t flags);
    uint32_t GetMaterialProgramInstanceFlags() const;

    void SetMaterialAlphaMode(RuntimeMaterialAlphaMode alphaMode);
    RuntimeMaterialAlphaMode GetMaterialAlphaMode() const;
    void SetAlphaCutoff(float alphaCutoff);
    float GetAlphaCutoff() const;
    void SetDoubleSided(bool doubleSided);
    bool IsDoubleSided() const;
    void SetUnlit(bool unlit);
    void SetShellSettings(const MaterialShellSettings& settings);
    const MaterialShellSettings& GetShellSettings() const noexcept { return shellSettings_; }

    void SetTextureSlot(MaterialTextureUsage usage, RuntimeTextureSlot slot);
    const RuntimeTextureSlot& GetTextureSlot(MaterialTextureUsage usage) const;
    bool HasTextureSlot(MaterialTextureUsage usage) const;
    void SetInheritModelTextures(bool inherit);
    bool InheritsModelTextures() const;

    void SetMetallicFactor(float value);
    float GetMetallicFactor() const;
    void SetRoughnessFactor(float value);
    float GetRoughnessFactor() const;
    void SetSpecularFactor(float value);
    float GetSpecularFactor() const;
    void SetSpecularColorFactor(const MATH::Vec3& value);
    const MATH::Vec3& GetSpecularColorFactor() const;
    // 表面の層：x クリアコート, y クリアコートの粗さ, z 透過, w 屈折率。
    void SetSurfaceLayers(const MATH::Vec4& value);
    const MATH::Vec4& GetSurfaceLayers() const noexcept { return surfaceLayers_; }
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
    void TouchRevision() noexcept;

    MATH::Vec4 baseColor_{1.0f, 1.0f, 1.0f, 1.0f};
    RuntimeTextureSlot baseColorTexture_{};
    RuntimeTextureSlot normalTexture_{};
    RuntimeTextureSlot metallicRoughnessTexture_{};
    RuntimeTextureSlot occlusionTexture_{};
    RuntimeTextureSlot emissiveTexture_{};
    RuntimeTextureSlot specularTexture_{};
    RuntimeTextureSlot specularColorTexture_{};
    RuntimeTextureSlot strokeMaskTexture_{};
    RuntimeTextureSlot strokeNormalTexture_{};
    bool inheritModelTextures_ = false;
    float metallicFactor_ = 0.0f;
    float roughnessFactor_ = 1.0f;
    float specularFactor_ = 1.0f;
    MATH::Vec3 specularColorFactor_{1.0f, 1.0f, 1.0f};
    MATH::Vec4 surfaceLayers_{0.0f, 0.1f, 0.0f, 1.5f};
    float normalScale_ = 1.0f;
    float occlusionStrength_ = 1.0f;
    MATH::Vec3 emissiveFactor_{0.0f, 0.0f, 0.0f};
    float emissiveStrength_ = 1.0f;
    std::string shaderProfileId_{};
    uint32_t featureBits_ = 0;
    RENDER3D::MATERIAL::MaterialProgramId materialProgramId_{RENDER3D::MATERIAL::kStandardPbrMaterialProgramId};
    std::vector<MATH::Vec4> materialProgramParameterBlock_{};
    std::shared_ptr<const RENDER3D::MATERIAL::MaterialParameterProjectionRecipe> materialParameterProjection_{};
    uint64_t materialParameterProjectionInstanceId_ = 0u;
    uint64_t materialParameterProjectionRevision_ = 0u;
    bool materialParameterProjectionApplied_ = false;
    std::vector<RuntimeTextureSlot> materialProgramResourceBlock_{};
    uint32_t materialProgramInstanceFlags_ = 0u;
    RuntimeMaterialAlphaMode materialAlphaMode_ = RuntimeMaterialAlphaMode::Opaque;
    float alphaCutoff_ = 0.5f;
    bool doubleSided_ = false;
    MaterialShellSettings shellSettings_{};
    uint64_t revision_ = 0;
};

} // namespace HIKARI
