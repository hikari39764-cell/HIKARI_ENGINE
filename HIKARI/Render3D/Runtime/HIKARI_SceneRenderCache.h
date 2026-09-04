#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>
#include <Vfx/Common/HIKARI_FxTypes.h>

#include "Animation/Runtime/HIKARI_AnimationPose.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Runtime/HIKARI_RenderModelAsset.h"
#include "Render3D/Water/HIKARI_WaterSurfaceRenderData.h"
#include "Render3D/Sand/HIKARI_SandSurfaceRenderData.h"

namespace HIKARI {
    class Material;
}

namespace HIKARI::RENDER3D::RUNTIME {

    struct SceneRenderObjectId {
        uint64_t value = 0;

        bool IsValid() const {
            return value != 0;
        }

        bool operator==(const SceneRenderObjectId& rhs) const {
            return value == rhs.value;
        }
    };

    struct SceneRenderMaterialOverride {
        uint32_t materialIndex = 0;
        const Material* material = nullptr;
        uint64_t materialRevision = 0;
    };

    struct SceneRenderObjectDesc {
        SceneRenderObjectId id{};

        const ModelAsset* model = nullptr;
        const RenderModelAsset* renderModel = nullptr;

        Transform3D worldTransform{};

        Bounds localBounds{};
        Bounds worldBounds{};
        // World-space padding for render-only vertex displacement such as
        // foliage wind. It affects culling bounds without changing geometry.
        MATH::Vec3 worldBoundsPadding{};

        bool visible = true;
        // Empty means every model node is visible. Otherwise entries map
        // directly to ModelAsset::nodes and use 0 for hidden, 1 for visible.
        std::vector<uint8_t> nodeVisibilityMask{};
        bool isStatic = false;
        bool castShadow = true;
        bool receiveShadow = true;
        bool hasRuntimeAnimation = false;
        std::shared_ptr<const ANIMATION::AnimationPoseSnapshot>
            animationPose{};
        uint64_t animationPoseRevision = 0u;
        bool hasSpecialRenderDebug = false;
        bool allowStaticCachedForward = true;
        std::string clusteredGeometryPath{};

        std::vector<SceneRenderMaterialOverride> materialOverrides{};
        uint32_t editorSelectionMaterialIndex = kInvalidRenderSurfaceIndex;

        std::string materialFxProfileId{};
        uint32_t postGroupMask = 0;

        DirectX::XMFLOAT4 materialFxParamValues[VFX::kMaterialFxUserCount]{};
        bool materialFxValuesInitialized = false;

        // Render-only foliage deformation contract. Parameters are
        // (strength metres, speed, spatial frequency, reserved) and reuse
        // SurfaceGpuScene::fxUser[0] without making MaterialFX the owner.
        bool foliageWind = false;
        MATH::Vec4 foliageWindParams{};

        // Renderer-owned typed input; Scene does not own the GPU slot layout.
        bool waterSurface = false;
        WATER::WaterSurfaceRenderData waterSurfaceData{};
        bool sandSurface = false;
        SAND::SandSurfaceRenderData sandSurfaceData{};
        std::shared_ptr<const std::vector<float>> sandSurfaceHeights{};
        std::shared_ptr<const std::vector<float>> sandSurfaceDeformations{};
        std::shared_ptr<const std::vector<float>> sandSurfaceLayerWeights{};
        std::shared_ptr<const std::vector<float>> sandSurfaceCoverage{};
        uint64_t sandSurfaceProjectionRevision = 0u;
    };

    struct SceneRenderObject {
        SceneRenderObjectDesc desc{};
        uint64_t lifetimeGeneration = 0u;

        bool valid = false;
        bool dirty = true;
        bool transformDirty = true;
        bool materialDirty = true;
        bool modelDirty = true;

        uint64_t lastTouchedFrame = 0;
        uint64_t version = 0;
    };

    struct SceneSurfaceInstance {
        SceneRenderObjectId objectId{};
        uint64_t objectLifetimeGeneration = 0u;
        uint64_t objectVersion = 0;
        uint32_t objectIndex = 0;
        uint32_t surfaceIndex = kInvalidRenderSurfaceIndex;

        const ModelAsset* model = nullptr;
        const RenderModelAsset* renderModel = nullptr;
        const RenderSurfaceRecord* surface = nullptr;

        uint32_t nodeIndex = kInvalidRenderSurfaceIndex;
        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;
        uint32_t materialIndex = 0;

        Transform3D objectWorldTransform{};
        MATH::Mat4 drawWorldMatrix{};
        MATH::Mat4 clusterWorldMatrix{};
        bool hasDrawWorldMatrix = false;

        Bounds localBounds{};
        Bounds worldBounds{};

        bool valid = false;
        bool visible = true;
        bool isStatic = false;
        bool castShadow = true;
        bool receiveShadow = true;
        bool hasRuntimeAnimation = false;
        std::shared_ptr<const ANIMATION::AnimationPoseSnapshot>
            animationPose{};
        uint64_t animationPoseRevision = 0u;
        bool hasSpecialRenderDebug = false;
        bool allowStaticCachedForward = true;
        bool skinned = false;
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
        WATER::WaterSurfaceRenderData waterSurfaceData{};
        bool sandSurface = false;
        // CPU-side residency only. A terrain whose atomic core could not be
        // admitted must never be submitted as the neutral 2x2 sand tile.
        bool sandSurfaceCoreResident = false;
        SAND::SandSurfaceRenderData sandSurfaceData{};
        std::shared_ptr<const std::vector<float>> sandSurfaceHeights{};
        std::shared_ptr<const std::vector<float>> sandSurfaceDeformations{};
        std::shared_ptr<const std::vector<float>> sandSurfaceLayerWeights{};
        std::shared_ptr<const std::vector<float>> sandSurfaceCoverage{};
        uint64_t sandSurfaceProjectionRevision = 0u;
        uint32_t sandSurfaceHeightOffset = 0u;
        uint32_t sandSurfaceDeformationOffset = 0u;
        uint32_t sandSurfaceLayerWeightsOffset = 0u;
        uint32_t sandSurfaceCoverageOffset = 0u;
        uint32_t sandSurfaceLayerTintsOffset = 0u;
        uint32_t waterSurfaceLiquidCoverageOffset = 0u;
    };

    class SceneRenderCache {
    public:
        struct Stats {
            uint32_t renderObjectCount = 0;
            uint32_t visibleObjectCount = 0;
            uint32_t hiddenObjectCount = 0;

            uint32_t dirtyObjectCount = 0;
            uint32_t staticObjectCount = 0;
            uint32_t dynamicObjectCount = 0;

            uint32_t insertedCount = 0;
            uint32_t updatedCount = 0;
            uint32_t removedCount = 0;
            uint32_t invalidDescCount = 0;

            uint32_t renderModelValidCount = 0;
            uint32_t renderModelInvalidCount = 0;

            uint32_t surfaceInstanceCount = 0;
            uint32_t visibleSurfaceInstanceCount = 0;
            uint32_t hiddenSurfaceInstanceCount = 0;
            uint32_t staticSurfaceInstanceCount = 0;
            uint32_t dynamicSurfaceInstanceCount = 0;
            uint32_t skinnedSurfaceInstanceCount = 0;
            uint32_t staticGeometrySurfaceInstanceCount = 0;
            uint32_t clusteredGeometrySurfaceInstanceCount = 0;
            uint32_t invalidSurfaceInstanceCount = 0;
            uint32_t missingSurfaceMatrixCount = 0;
            uint32_t invalidSurfaceBoundsCount = 0;

            uint32_t dirtySurfaceInstanceCount = 0;
        };

        void Clear();

        void BeginSync(uint64_t frameIndex);
        void Upsert(const SceneRenderObjectDesc& desc);
        void EndSync();
        void BeginPatchSync(uint64_t frameIndex);
        void EndPatchSync();

        void Remove(SceneRenderObjectId id);
        void OnSceneLoaded();
        void OnSceneUnloaded();
        void MarkAllDirty();
        void PreRenderSync();

        const SceneRenderObject* Find(SceneRenderObjectId id) const;
        const std::vector<SceneRenderObject>& GetObjects() const;
        const std::vector<SceneSurfaceInstance>& GetSurfaceInstances() const;
        const std::vector<float>* GetSandSurfaceHeights() const;
        const std::vector<uint32_t>& GetDirtySurfaceIndices() const;
        uint64_t GetSurfaceVersion() const;
        uint64_t GetSurfaceRoutingVersion() const;
        uint64_t GetSurfaceDataVersion() const;
        uint64_t GetSurfaceFloatPoolVersion() const;
        const Stats& GetStats() const;

    private:
        void RemoveAt(size_t index);
        void RebuildIndex();
        void RebuildSurfaceInstances();
        void UpdateSurfaceInstancesForObject(size_t objectIndex);
        void MarkSurfaceTopologyDirty();
        void MarkSurfaceRoutingDirty();
        void MarkSurfaceDataDirty();
        void MarkDirtySurfaceIndex(uint32_t surfaceIndex);
        void MarkAllSurfaceInstancesDirty();
        void RefreshStats();

        std::vector<SceneRenderObject> objects_{};
        std::vector<SceneSurfaceInstance> surfaceInstances_{};
        std::vector<float> sandSurfaceHeights_{};
        std::vector<uint32_t> dirtySurfaceIndices_{};
        std::unordered_map<uint64_t, size_t> indexById_{};

        Stats frameStats_{};
        uint64_t currentSyncFrame_ = 0;
        uint64_t surfaceVersion_ = 0;
        uint64_t surfaceRoutingVersion_ = 0;
        uint64_t surfaceDataVersion_ = 0;
        uint64_t surfaceFloatPoolVersion_ = 0;
        bool surfaceTopologyDirty_ = true;
        bool surfaceRoutingDirty_ = true;
        bool surfaceDataDirty_ = true;
        bool surfaceFloatPoolDirty_ = true;
        std::vector<std::array<uint32_t, 10u>>
            previousPackedSurfaceScratch_{};
        std::vector<uint32_t> sourceSandGridResolutionScratch_{};
        std::vector<uint8_t> terrainCoreResidentScratch_{};
        std::vector<float> surfaceTintScratch_{};
    };

}
