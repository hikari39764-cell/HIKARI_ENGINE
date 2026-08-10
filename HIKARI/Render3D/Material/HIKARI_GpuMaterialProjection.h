#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"

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

    struct GpuMaterialTextureProjection {
        int32_t backendHandle = -1;
        uint32_t descriptorIndex = kInvalidTextureDescriptorIndex;
        MATH::Vec4 uvTransform{1.0f, 1.0f, 0.0f, 0.0f};
        float uvRotation = 0.0f;
        uint32_t uvSet = 0u;
        bool enabled = false;
    };

    // Material-owned projection into the shared C++/HLSL layout. Renderers
    // resolve scene/model override policy before filling this value; packing
    // and descriptor semantics remain centralized here.
    struct GpuMaterialProjection {
        MATH::Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
        MATH::Vec4 emissiveFactor{};
        MATH::Vec4 pbrParams{0.0f, 1.0f, 1.0f, 0.5f};
        MATH::Vec4 specularParams{1.0f, 1.0f, 1.0f, 1.0f};
        uint32_t materialFlags = 0u;
        float normalScale = 1.0f;
        std::array<
            GpuMaterialTextureProjection,
            static_cast<size_t>(GpuMaterialTextureChannel::Count)> textures{};
    };

    GpuMaterialProjection BuildRuntimeMaterialGpuProjection(
        const Material& material);

    GpuMaterialData PackGpuMaterialData(
        const GpuMaterialProjection& projection);

    std::vector<uint32_t>
        ResolveMaterialProgramResourceDescriptorIndices(
            const Material& material);

} // namespace HIKARI::RENDER3D::MATERIAL
