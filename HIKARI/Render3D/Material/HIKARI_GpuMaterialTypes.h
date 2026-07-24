#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::MATERIAL {

    constexpr uint32_t kInvalidGpuMaterialIndex = 0xffffffffu;
    constexpr uint32_t kInvalidTextureDescriptorIndex = 0xffffffffu;
    constexpr uint32_t kDefaultGpuMaterialCapacity = 4096u;

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
        float materialPadding0[3]{};
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
        uint32_t materialPadding1[2]{};
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

} // namespace HIKARI::RENDER3D::MATERIAL
