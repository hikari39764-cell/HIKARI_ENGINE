#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <DirectXMath.h>

#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPlan.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"
#include "Render3D/Sand/HIKARI_SandSurfaceRenderData.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI {
class Material;
}

namespace HIKARI::RENDER3D::GPUDRIVEN {

enum class GpuSceneSurfacePassFlags : uint8_t {
    None = 0,
    Forward = 1u << 0,
    Shadow = 1u << 1,
};

struct GpuSceneResourceKey {
    uint32_t materialIndex = 0;
    uint32_t meshIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    uint32_t primitiveIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    uint32_t passMask = 0;
    bool hasMaterialOverride = false;
    bool skinned = false;
    RUNTIME::SurfaceGeometryBackend geometryBackend = RUNTIME::SurfaceGeometryBackend::TriangleMesh;
    RUNTIME::SurfaceBackendRoute backendRoute = RUNTIME::SurfaceBackendRoute::Unsupported;

    uint64_t modelKey = 0;
    uint64_t geometryKey = 0;
    uint64_t clusterGeometryKey = 0;
    uint64_t materialKey = 0;
    // Runtime 内容の世代。materialKey とは分離し、編集のたびに仮想
    // Resource identity が増え続けないようにする。
    uint64_t materialRevision = 0;
    uint64_t textureSetKey = 0;
    uint64_t shaderKey = 0;
    uint64_t psoKey = 0;
    uint64_t sortKey = 0;
    RUNTIME::SurfaceResourceIds resources{};

    bool resourceKeyValid = false;
    bool objectDataCompatible = false;
    bool clusterMainlineEligible = false;
    bool materialFx = false;
    bool waterMaterialFx = false;
    bool sandSurface = false;
    bool materialFxUsesCustomVertexShader = false;
    bool materialFxUsesCustomPixelShader = false;
    bool customVertexShader = false;
    bool depthAware = false;
    bool alphaMasked = false;
    bool transparent = false;
    bool doubleSided = false;
};

struct GpuSceneSurfaceRecord {
    RUNTIME::SceneRenderObjectId objectId{};
    uint64_t objectLifetimeGeneration = 0u;
    uint64_t objectVersion = 0;
    uint32_t sourceSurfaceInstanceIndex = RUNTIME::kInvalidRenderSurfaceIndex;

    const RUNTIME::SceneSurfaceInstance* sourceSurface = nullptr;
    const ModelAsset* model = nullptr;
    const RUNTIME::RenderModelAsset* renderModel = nullptr;
    const RUNTIME::RenderSurfaceRecord* surface = nullptr;

    uint32_t surfaceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    uint32_t nodeIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    uint32_t meshIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    uint32_t primitiveIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    uint32_t materialIndex = 0;

    Transform3D objectWorldTransform{};
    MATH::Mat4 drawWorldMatrix{};
    MATH::Mat4 clusterWorldMatrix{};
    bool hasDrawWorldMatrix = false;
    Bounds worldBounds{};

    bool valid = false;
    bool visible = true;
    bool isStatic = false;
    bool hasRuntimeAnimation = false;
    std::shared_ptr<const ANIMATION::AnimationPoseSnapshot> animationPose{};
    uint64_t animationPoseRevision = 0u;
    bool hasSpecialRenderDebug = false;
    bool skinned = false;
    uint32_t jointPaletteSlot = RUNTIME::kInvalidRenderSurfaceIndex;
    uint32_t jointPaletteMatrixCount = 0;
    bool castShadow = true;
    bool receiveShadow = true;
    bool forwardCandidate = false;
    bool shadowCandidate = false;
    std::string clusteredGeometryPath{};

    const Material* materialOverride = nullptr;
    uint64_t materialOverrideRevision = 0;
    bool editorSelectionOutline = false;
    std::string materialFxProfileId{};
    uint32_t postGroupMask = 0;
    DirectX::XMFLOAT4 materialFxParamValues[VFX::kMaterialFxUserCount]{};
    bool materialFxValuesInitialized = false;
    bool foliageWind = false;
    MATH::Vec4 foliageWindParams{};
    bool waterSurface = false;
    RENDER3D::WATER::WaterSurfaceRenderData waterSurfaceData{};
    uint32_t waterSurfaceGpuDataIndex = 0u;
    bool sandSurface = false;
    SAND::SandSurfaceRenderData sandSurfaceData{};
    uint32_t sandSurfaceGpuDataIndex = 0u;
    uint32_t sandSurfaceHeightOffset = 0u;
    uint32_t sandSurfaceDeformationOffset = 0u;
    uint32_t sandSurfaceLayerWeightsOffset = 0u;
    uint32_t sandSurfaceCoverageOffset = 0u;
    uint32_t sandSurfaceLayerTintsOffset = 0u;
    uint32_t waterSurfaceLiquidCoverageOffset = 0u;

    GpuSceneResourceKey key{};
};

struct GpuSceneSurfaceValidationResult {
    bool invalidSource = false;
    bool invalidModel = false;
    bool unsupportedGeometry = false;
    bool missingDrawMatrix = false;
    bool invalidBounds = false;
    bool invalidPrimitiveIndex = false;
    bool unsupportedWaterSkinned = false;
    bool unsupportedWaterCustomVertexShader = false;
    bool unsupportedWaterCustomPixelShader = false;

    bool IsValid() const;
};

GpuSceneSurfaceRecord BuildGpuSceneSurfaceRecord(const RUNTIME::SceneSurfaceInstance& surfaceInstance,
                                                 uint32_t sourceSurfaceInstanceIndex);

GpuSceneSurfaceValidationResult ValidateGpuSceneSurfaceRecord(const GpuSceneSurfaceRecord& record);

bool HasValidGpuSceneSubmitPrimitiveTarget(const GpuSceneSurfaceRecord& record);

bool IsGpuSceneForwardOpaqueResidentRecord(const GpuSceneSurfaceRecord& record);

bool IsGpuSceneForwardDepthAwareResidentRecord(const GpuSceneSurfaceRecord& record);

bool IsGpuSceneForwardTransparentResidentRecord(const GpuSceneSurfaceRecord& record);

bool IsGpuSceneShadowResidentRecord(const GpuSceneSurfaceRecord& record);

RUNTIME::SurfaceGpuSceneMaterialSource BuildGpuSceneMaterialSource(const GpuSceneSurfaceRecord& record,
                                                                   const RUNTIME::SurfaceGpuSceneInstance& instance);

RUNTIME::SurfaceGpuSceneBuildStats BuildGpuSceneInstanceList(
    const std::vector<GpuSceneSurfaceRecord>& records, const std::vector<uint32_t>& recordIndices,
    std::vector<RUNTIME::SurfaceGpuSceneInstance>& outInstances,
    std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>& outMaterialSources);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
