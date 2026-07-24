#pragma once

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

    struct SceneRenderObjectDesc {
        SceneRenderObjectId id{};

        const ModelAsset* model = nullptr;
        const RenderModelAsset* renderModel = nullptr;

        Transform3D worldTransform{};

        Bounds localBounds{};
        Bounds worldBounds{};

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
        std::string clusteredGeometryPath{};

        const Material* materialOverride = nullptr;
        uint64_t materialOverrideRevision = 0;

        std::string materialFxProfileId{};
        uint32_t postGroupMask = 0;

        DirectX::XMFLOAT4 materialFxParamValues[VFX::kMaterialFxUserCount]{};
        bool materialFxValuesInitialized = false;
    };

    struct SceneRenderObject {
        SceneRenderObjectDesc desc{};

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
        std::string materialFxProfileId{};
        uint32_t postGroupMask = 0;
        DirectX::XMFLOAT4 materialFxParamValues[VFX::kMaterialFxUserCount]{};
        bool materialFxValuesInitialized = false;
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
        const std::vector<uint32_t>& GetDirtySurfaceIndices() const;
        uint64_t GetSurfaceVersion() const;
        uint64_t GetSurfaceRoutingVersion() const;
        uint64_t GetSurfaceDataVersion() const;
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
        std::vector<uint32_t> dirtySurfaceIndices_{};
        std::unordered_map<uint64_t, size_t> indexById_{};

        Stats frameStats_{};
        uint64_t currentSyncFrame_ = 0;
        uint64_t surfaceVersion_ = 0;
        uint64_t surfaceRoutingVersion_ = 0;
        uint64_t surfaceDataVersion_ = 0;
        bool surfaceTopologyDirty_ = true;
        bool surfaceRoutingDirty_ = true;
        bool surfaceDataDirty_ = true;
    };

}
