#include "Render3D/Runtime/HIKARI_SurfaceDrawRoute.h"

#include "Render3D/Core/HIKARI_Material.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI::RENDER3D::RUNTIME {

    bool IsSurfaceObjectDataVertexShader(std::string_view vertexShaderId) {
        return
            vertexShaderId.empty() ||
            vertexShaderId == "Render3D_StaticVS" ||
            vertexShaderId == "Render3D_FxWaterVS";
    }

    bool IsSurfaceObjectDataPixelShader(std::string_view shaderId, std::string_view pixelShaderId) {
        const std::string_view id = !pixelShaderId.empty() ? pixelShaderId : shaderId;
        return id.empty() ||
            id == "PBR" ||
            id == "StaticLit" ||
            id == "StaticFx" ||
            id == "MaterialFx" ||
            id == "Render3D_StaticPS" ||
            id == "Render3D_StaticFxPS" ||
            id == "Render3D_FxWaterPS" ||
            id == "Render3D_GeometryAuxPS";
    }

    SurfaceDrawShaderRoute ResolveSurfaceDrawShaderRoute(
        const Material* materialOverride,
        const MaterialAsset* materialAsset,
        std::string_view materialFxProfileId) {

        SurfaceDrawShaderRoute route{};
        if (materialOverride != nullptr) {
            route.shaderProfileId = materialOverride->GetShaderProfileId();
            if (route.shaderProfileId.empty()) {
                route.shaderProfileId = "PBR";
            }
            route.featureBits = materialOverride->GetFeatureBits();
        } else if (materialAsset != nullptr) {
            route.shaderProfileId = materialAsset->shaderProfileId.empty() ? "PBR" : materialAsset->shaderProfileId;
            route.featureBits = materialAsset->featureBits;
            route.alphaMode = materialAsset->alphaMode;
            route.doubleSided = MATERIAL_POLICY::ShouldRenderDoubleSided(*materialAsset);
        }

        if (!materialFxProfileId.empty()) {
            MaterialFxProfile profile{};
            if (MaterialFxProfile::LoadById(std::string(materialFxProfileId), profile)) {
                if (!profile.shaderProfileId.empty()) {
                    route.shaderProfileId = profile.shaderProfileId;
                }
                if (!profile.vertexShaderId.empty()) {
                    route.vertexShaderId = profile.vertexShaderId;
                }
                if (!profile.pixelShaderId.empty()) {
                    route.pixelShaderId = profile.pixelShaderId;
                }
                route.featureBits = profile.featureBits;
                route.profileDoubleSided = profile.doubleSided;
                route.doubleSided = route.doubleSided || profile.doubleSided;
                route.depthAware = profile.renderPhase == MaterialFxRenderPhase::DepthAware;
                if (materialAsset != nullptr &&
                    (materialAsset->featureBits & MATERIAL_FEATURES::AlphaMask) != 0) {
                    route.featureBits |= MATERIAL_FEATURES::AlphaMask;
                }
            }
        }

        route.objectDataCompatible =
            IsSurfaceObjectDataVertexShader(route.vertexShaderId) &&
            IsSurfaceObjectDataPixelShader(route.shaderProfileId, route.pixelShaderId);
        return route;
    }

} // namespace HIKARI::RENDER3D::RUNTIME
