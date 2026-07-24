#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI {
    class Material;
}

namespace HIKARI::RENDER3D::RUNTIME {

    struct SurfaceDrawShaderRoute {
        std::string shaderProfileId = "PBR";
        std::string vertexShaderId{};
        std::string pixelShaderId{};
        uint32_t featureBits = 0;
        AlphaMode alphaMode = AlphaMode::Opaque;
        bool doubleSided = false;
        bool profileDoubleSided = false;
        bool objectDataCompatible = false;
        bool depthAware = false;
    };

    bool IsSurfaceObjectDataVertexShader(std::string_view vertexShaderId);
    bool IsSurfaceObjectDataPixelShader(std::string_view shaderId, std::string_view pixelShaderId);

    SurfaceDrawShaderRoute ResolveSurfaceDrawShaderRoute(
        const Material* materialOverride,
        const MaterialAsset* materialAsset,
        std::string_view materialFxProfileId);

} // namespace HIKARI::RENDER3D::RUNTIME
