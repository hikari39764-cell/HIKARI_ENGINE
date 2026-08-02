#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::MATERIAL {

    constexpr uint32_t kInvalidGpuMaterialIndex = 0xffffffffu;
    constexpr uint32_t kInvalidTextureDescriptorIndex = 0xffffffffu;
    constexpr uint32_t kDefaultGpuMaterialCapacity = 4096u;
    constexpr uint32_t kGpuMaterialProgramParameterSlotCount = 20u;
    constexpr uint32_t kGpuMaterialProgramResourceSlotCount = 8u;
    constexpr uint32_t kGpuMaterialProgramParameterCapacity =
        kDefaultGpuMaterialCapacity * kGpuMaterialProgramParameterSlotCount;
    constexpr uint32_t kGpuMaterialProgramResourceCapacity =
        kDefaultGpuMaterialCapacity * kGpuMaterialProgramResourceSlotCount;

    using GpuMaterialProgramParameterBlock =
        std::array<MATH::Vec4, kGpuMaterialProgramParameterSlotCount>;
    using GpuMaterialProgramResourceBlock =
        std::array<uint32_t, kGpuMaterialProgramResourceSlotCount>;

    namespace GPU_MATERIAL_PROGRAM_FLAGS {
        inline constexpr uint32_t Unlit = 1u << 0u;
        inline constexpr uint32_t AlphaMask = 1u << 1u;
        inline constexpr uint32_t AlphaBlend = 1u << 2u;
        inline constexpr uint32_t DoubleSided = 1u << 3u;
        inline constexpr uint32_t UsedLastGood = 1u << 4u;
        inline constexpr uint32_t UsedFallback = 1u << 5u;
        inline constexpr uint32_t StandardPbr = 1u << 6u;
        inline constexpr uint32_t RampDiffuse = 1u << 7u;
        inline constexpr uint32_t MultiRampDiffuse = 1u << 8u;
        inline constexpr uint32_t SpecularBand = 1u << 9u;
        inline constexpr uint32_t FresnelRim = 1u << 10u;
        inline constexpr uint32_t MatCap = 1u << 11u;
        inline constexpr uint32_t AnisotropicHair = 1u << 12u;
        inline constexpr uint32_t FaceSdf = 1u << 13u;
        inline constexpr uint32_t EyeLighting = 1u << 14u;
        inline constexpr uint32_t UsesProgramBlock = 1u << 15u;
        inline constexpr uint32_t TemplateGenericToon = 1u << 16u;
        inline constexpr uint32_t TemplateSkin = 1u << 17u;
        inline constexpr uint32_t TemplateFace = 1u << 18u;
        inline constexpr uint32_t TemplateHair = 1u << 19u;
        inline constexpr uint32_t TemplateEye = 1u << 20u;
        inline constexpr uint32_t TemplateCloth = 1u << 21u;
        inline constexpr uint32_t TemplateHybrid = 1u << 22u;
        inline constexpr uint32_t RenderFeatureGeometryOutline = 1u << 23u;
        inline constexpr uint32_t RenderFeatureScreenOutline = 1u << 24u;
        inline constexpr uint32_t RenderFeatureVisibilityOverlay = 1u << 25u;
        inline constexpr uint32_t RenderFeatureFaceHairShadow = 1u << 26u;
        // Editor-only transient preview flag. Never serialized by Material assets.
        inline constexpr uint32_t EditorSelectionOutline = 1u << 27u;
        inline constexpr uint32_t HairControlMap = 1u << 28u;
        inline constexpr uint32_t EyeControlMap = 1u << 29u;
    }

    struct GpuMaterialProgramBinding {
        uint32_t materialProgramIndex = 0u;
        uint32_t parameterBlockOffset = 0u;
        uint32_t resourceTableOffset = 0u;
        uint32_t parameterBlockCount = 0u;
        uint32_t instanceFlags = 0u;

        friend bool operator==(
            const GpuMaterialProgramBinding& lhs,
            const GpuMaterialProgramBinding& rhs) = default;
    };

    static_assert(sizeof(GpuMaterialProgramBinding) == 5u * sizeof(uint32_t));

    struct GpuMaterialUploadRange {
        uint32_t firstSlot = 0u;
        uint32_t slotCount = 0u;
    };

    // StructuredBuffer layout shared by every material-consuming render pass.
    struct GpuMaterialData {
        MATH::Vec4 baseColor{};
        MATH::Vec4 emissiveFactor{};
        MATH::Vec4 pbrParams{}; // x: metallic, y: roughness, z: occlusion, w: alpha cutoff
        MATH::Vec4 specularParams{}; // xyz: specular color factor, w: specular factor
        uint32_t materialFlags = 0;
        uint32_t hasBaseColorTexture = 0;
        uint32_t hasNormalTexture = 0;
        uint32_t hasEmissiveTexture = 0;
        uint32_t hasMetallicRoughnessTexture = 0;
        uint32_t hasOcclusionTexture = 0;
        uint32_t hasSpecularTexture = 0;
        uint32_t hasSpecularColorTexture = 0;
        float normalScale = 1.0f;
        uint32_t materialProgramIndex = 0u;
        uint32_t parameterBlockOffset = 0u;
        uint32_t resourceTableOffset = 0u;
        int32_t baseColorTextureHandle = -1;
        int32_t normalTextureHandle = -1;
        int32_t emissiveTextureHandle = -1;
        int32_t metallicRoughnessTextureHandle = -1;
        int32_t occlusionTextureHandle = -1;
        int32_t specularTextureHandle = -1;
        int32_t specularColorTextureHandle = -1;
        uint32_t baseColorTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t normalTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t emissiveTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t metallicRoughnessTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t occlusionTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t specularTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t specularColorTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t parameterBlockCount = 0u;
        uint32_t instanceFlags = 0u;
        MATH::Vec4 baseColorUvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
        MATH::Vec4 normalUvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
        MATH::Vec4 emissiveUvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
        MATH::Vec4 metallicRoughnessUvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
        MATH::Vec4 occlusionUvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
        MATH::Vec4 specularUvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
        MATH::Vec4 specularColorUvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
        MATH::Vec4 uvRotation0{}; // x: baseColor, y: normal, z: emissive, w: metallicRoughness
        MATH::Vec4 uvRotation1{}; // x: occlusion, y: specular, z: specularColor
        uint32_t uvSet0[4]{}; // x: baseColor, y: normal, z: emissive, w: metallicRoughness
        uint32_t uvSet1[4]{}; // x: occlusion, y: specular, z: specularColor
    };

    static_assert(sizeof(GpuMaterialData) == 352u);
    static_assert(offsetof(GpuMaterialData, materialProgramIndex) == 100u);
    static_assert(offsetof(GpuMaterialData, parameterBlockOffset) == 104u);
    static_assert(offsetof(GpuMaterialData, resourceTableOffset) == 108u);
    static_assert(offsetof(GpuMaterialData, parameterBlockCount) == 168u);
    static_assert(offsetof(GpuMaterialData, instanceFlags) == 172u);

} // namespace HIKARI::RENDER3D::MATERIAL
