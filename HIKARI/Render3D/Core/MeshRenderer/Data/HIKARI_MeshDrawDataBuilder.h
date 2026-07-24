#pragma once

#include <cstddef>
#include <vector>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI {
    struct MaterialAsset;
}

namespace HIKARI::MESHRENDERER {

    struct MeshMaterialFillContext {
        int fallbackTextureHandle = -1;
        int fallbackNormalTextureHandle = -1;
        int fallbackBlackTextureHandle = -1;
        MeshRendererDebugStats* stats = nullptr;
    };

    void FillLightCB(
        const SceneEnvironment& environment,
        RenderDebugView debugView,
        LightCB& out,
        MeshRendererDebugStats& stats);
    void FillShadowCB(const SceneEnvironment& environment, ShadowCB& out);
    void FillSkyEnvironmentCB(const SceneEnvironment& environment, SkyEnvironmentCB& out);

    MATH::Mat4 BuildNormalMatrix(const Transform3D& transform);

    void FillFxValues(ObjectCB& obj, const DrawItem& item);
    void FillMaterialValues(
        ObjectCB& obj,
        const MaterialAsset* materialAsset,
        int normalTextureHandle,
        int emissiveTextureHandle,
        int metallicRoughnessTextureHandle,
        int occlusionTextureHandle,
        const MeshMaterialFillContext& ctx);

    size_t UploadJointPalette(
        JointPaletteCB* jointPaletteMapped,
        size_t objectIndex,
        const std::vector<MATH::Mat4>& jointPalette);

} // namespace HIKARI::MESHRENDERER
