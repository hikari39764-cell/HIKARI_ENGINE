#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "RenderContracts/Model/HIKARI_ModelAsset.h"

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
    bool objectDataCompatible = false;
    bool depthAware = false;
};

bool IsSurfaceObjectDataVertexShader(std::string_view vertexShaderId);
bool IsSurfaceObjectDataPixelShader(std::string_view shaderId, std::string_view pixelShaderId);

// primitiveAsset があればモデル材質の両面判定に面の形（薄い板か）も使う（無ければ材質だけで決める）。
SurfaceDrawShaderRoute ResolveSurfaceDrawShaderRoute(const Material* materialOverride,
                                                     const MaterialAsset* materialAsset,
                                                     const MeshPrimitive* primitiveAsset,
                                                     std::string_view materialFxProfileId);

} // namespace HIKARI::RENDER3D::RUNTIME
