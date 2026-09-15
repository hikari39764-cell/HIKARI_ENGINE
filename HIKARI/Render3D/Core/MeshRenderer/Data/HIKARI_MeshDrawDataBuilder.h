#pragma once

#include <cstddef>
#include <vector>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI {
    struct MaterialAsset;
    namespace GFX { struct Context; }
    namespace DX { class DynamicUploadBuffer; }
}

namespace HIKARI::MESHRENDERER {

    struct MeshMaterialFillContext {
        int fallbackTextureHandle = -1;
        int fallbackNormalTextureHandle = -1;
        int fallbackBlackTextureHandle = -1;
        MeshRendererDebugStats* stats = nullptr;
    };

    void FillCameraCB(
        const MATH::Mat4& viewProj,
        const MATH::Mat4& inverseViewProj,
        const MATH::Vec3& cameraPosition,
        uint32_t width,
        uint32_t height,
        CameraCB& out);

    void FillLightCB(
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::LocalLightListView& localLightList,
        RenderDebugView debugView,
        LightCB& out,
        MeshRendererDebugStats& stats);
    void FillShadowCB(
        const SceneEnvironment& environment,
        uint64_t frameIndex,
        ShadowCB& out);
    void FillSkyEnvironmentCB(
        const SceneEnvironment& environment,
        const MATH::Vec3& cameraPosition,
        const MATH::Vec3& viewFocusPosition,
        SkyEnvironmentCB& out);
    void FillSelectedIrradianceVolumes(SkyEnvironmentCB& out);

    MATH::Mat4 BuildNormalMatrix(const Transform3D& transform);

    void FillFxValues(ObjectCB& obj, const DrawItem& item);
    void FillMaterialFactorValues(ObjectCB& obj,
        const MaterialAsset* modelMaterial, const Material* runtimeMaterial);
    void FillMaterialValues(
        ObjectCB& obj,
        const MaterialAsset* materialAsset,
        int normalTextureHandle,
        int emissiveTextureHandle,
        int metallicRoughnessTextureHandle,
        int occlusionTextureHandle,
        const MeshMaterialFillContext& ctx);

    D3D12_GPU_VIRTUAL_ADDRESS UploadJointPalettes(
        const GFX::Context& graphics,
        DX::DynamicUploadBuffer& uploads,
        const std::vector<MATH::Mat4>* palettes,
        size_t paletteCount);

} // namespace HIKARI::MESHRENDERER
