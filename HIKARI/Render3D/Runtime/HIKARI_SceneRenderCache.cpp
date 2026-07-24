#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

#include <algorithm>
#include <limits>

#include "Core/Numeric/HIKARI_IntegerConversion.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceResolver.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        bool EqualVec3(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        bool EqualQuat(const MATH::Quat& lhs, const MATH::Quat& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
        }

        bool EqualMat4(const MATH::Mat4& lhs, const MATH::Mat4& rhs) {
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    if (lhs.m[col][row] != rhs.m[col][row]) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool EqualBounds(const Bounds& lhs, const Bounds& rhs) {
            return EqualVec3(lhs.min, rhs.min) && EqualVec3(lhs.max, rhs.max);
        }

        bool EqualTransform(const Transform3D& lhs, const Transform3D& rhs) {
            return
                EqualVec3(lhs.position, rhs.position) &&
                EqualQuat(lhs.rotation, rhs.rotation) &&
                EqualVec3(lhs.scale, rhs.scale) &&
                lhs.GetParent() == rhs.GetParent() &&
                lhs.useExplicitMatrix == rhs.useExplicitMatrix &&
                EqualMat4(lhs.explicitMatrix, rhs.explicitMatrix);
        }

        bool EqualMaterialFxValues(const DirectX::XMFLOAT4* lhs, const DirectX::XMFLOAT4* rhs) {
            for (int i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                if (lhs[i].x != rhs[i].x ||
                    lhs[i].y != rhs[i].y ||
                    lhs[i].z != rhs[i].z ||
                    lhs[i].w != rhs[i].w) {
                    return false;
                }
            }
            return true;
        }

        struct DirtyFlags {
            bool any = false;
            bool transform = false;
            bool material = false;
            bool model = false;
            bool routing = false;
        };

        DirtyFlags BuildDirtyFlags(const SceneRenderObjectDesc& oldDesc, const SceneRenderObjectDesc& newDesc) {
            DirtyFlags flags{};

            flags.model =
                oldDesc.model != newDesc.model ||
                oldDesc.renderModel != newDesc.renderModel ||
                oldDesc.clusteredGeometryPath != newDesc.clusteredGeometryPath ||
                !EqualBounds(oldDesc.localBounds, newDesc.localBounds);

            flags.transform =
                !EqualTransform(oldDesc.worldTransform, newDesc.worldTransform) ||
                !EqualBounds(oldDesc.worldBounds, newDesc.worldBounds);

            flags.material =
                oldDesc.materialOverride != newDesc.materialOverride ||
                oldDesc.materialOverrideRevision != newDesc.materialOverrideRevision ||
                oldDesc.materialFxProfileId != newDesc.materialFxProfileId ||
                oldDesc.postGroupMask != newDesc.postGroupMask ||
                oldDesc.materialFxValuesInitialized != newDesc.materialFxValuesInitialized ||
                !EqualMaterialFxValues(oldDesc.materialFxParamValues, newDesc.materialFxParamValues);

            const bool animationDataChanged =
                oldDesc.animationPoseRevision !=
                    newDesc.animationPoseRevision ||
                oldDesc.animationPose != newDesc.animationPose;

            flags.routing =
                flags.material ||
                oldDesc.visible != newDesc.visible ||
                oldDesc.isStatic != newDesc.isStatic ||
                oldDesc.castShadow != newDesc.castShadow ||
                oldDesc.receiveShadow != newDesc.receiveShadow ||
                oldDesc.hasRuntimeAnimation != newDesc.hasRuntimeAnimation ||
                oldDesc.hasSpecialRenderDebug != newDesc.hasSpecialRenderDebug ||
                oldDesc.allowStaticCachedForward != newDesc.allowStaticCachedForward;

            flags.any =
                flags.model ||
                flags.transform ||
                flags.routing ||
                animationDataChanged;

            return flags;
        }

        bool IsInvalidStoredDesc(const SceneRenderObjectDesc& desc) {
            if (!desc.id.IsValid() || desc.model == nullptr) {
                return true;
            }
            return !BOUNDS::IsUsable(desc.localBounds) || !BOUNDS::IsUsable(desc.worldBounds);
        }

        void CopyMaterialFxValues(const SceneRenderObjectDesc& desc, SceneSurfaceInstance& instance) {
            for (int i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                instance.materialFxParamValues[i] = desc.materialFxParamValues[i];
            }
        }

        bool IsSurfaceSourceUsable(const SceneRenderObject& object) {
            return
                object.valid &&
                object.desc.id.IsValid() &&
                object.desc.model != nullptr &&
                object.desc.renderModel != nullptr &&
                object.desc.renderModel->valid;
        }

        std::vector<MATH::Mat4> BuildObjectNodeGlobals(const SceneRenderObject& object) {
            const RenderModelAsset& renderModel = *object.desc.renderModel;
            if (!object.desc.hasRuntimeAnimation) {
                return BuildRenderModelNodeGlobals(renderModel);
            }

            return BuildRenderModelNodeGlobals(
                renderModel,
                object.desc.model,
                object.desc.animationPose->localPose);
        }

        SceneSurfaceInstance BuildSurfaceInstance(
            const SceneRenderObject& object,
            uint32_t objectIndex,
            const RenderSurfaceRecord& surface,
            uint32_t surfaceIndex,
            const std::vector<MATH::Mat4>& nodeGlobals) {

            SceneSurfaceInstance instance{};
            instance.objectId = object.desc.id;
            instance.objectVersion = object.version;
            instance.objectIndex = objectIndex;
            instance.surfaceIndex = surfaceIndex;
            instance.model = object.desc.model;
            instance.renderModel = object.desc.renderModel;
            instance.surface = &surface;
            instance.nodeIndex = surface.nodeIndex;
            instance.meshIndex = surface.meshIndex;
            instance.primitiveIndex = surface.primitiveIndex;
            instance.materialIndex = surface.materialIndex;
            instance.objectWorldTransform = object.desc.worldTransform;
            instance.localBounds = surface.localBounds;
            instance.visible = object.desc.visible;
            instance.isStatic = object.desc.isStatic;
            instance.castShadow = object.desc.castShadow && surface.castShadowDefault;
            instance.receiveShadow = object.desc.receiveShadow && surface.receiveShadowDefault;
            instance.hasRuntimeAnimation = object.desc.hasRuntimeAnimation;
            instance.animationPose = object.desc.animationPose;
            instance.animationPoseRevision =
                object.desc.animationPoseRevision;
            instance.hasSpecialRenderDebug = object.desc.hasSpecialRenderDebug;
            instance.allowStaticCachedForward = object.desc.allowStaticCachedForward;
            instance.skinned = surface.IsSkinned();
            instance.clusteredGeometryPath = object.desc.clusteredGeometryPath;
            instance.materialOverride = object.desc.materialOverride;
            instance.materialOverrideRevision =
                object.desc.materialOverrideRevision;
            instance.materialFxProfileId = object.desc.materialFxProfileId;
            instance.postGroupMask = object.desc.postGroupMask;
            instance.materialFxValuesInitialized = object.desc.materialFxValuesInitialized;
            CopyMaterialFxValues(object.desc, instance);

            instance.hasDrawWorldMatrix = ResolveRenderSurfaceDrawWorldMatrix(
                object.desc.worldTransform,
                surface,
                nodeGlobals,
                instance.drawWorldMatrix);
            instance.worldBounds = ResolveRenderSurfaceWorldBounds(
                object.desc.worldBounds,
                surface,
                instance.drawWorldMatrix,
                instance.hasDrawWorldMatrix);

            instance.valid =
                IsSurfaceSourceUsable(object) &&
                HasValidRenderSurfacePrimitive(object.desc.model, surface) &&
                instance.hasDrawWorldMatrix &&
                BOUNDS::IsUsable(instance.worldBounds);
            return instance;
        }
    }

    void SceneRenderCache::Clear() {
        objects_.clear();
        surfaceInstances_.clear();
        dirtySurfaceIndices_.clear();
        indexById_.clear();
        frameStats_ = {};
        currentSyncFrame_ = 0;
        surfaceVersion_ = 0;
        surfaceRoutingVersion_ = 0;
        surfaceDataVersion_ = 0;
        surfaceTopologyDirty_ = true;
        surfaceRoutingDirty_ = true;
        surfaceDataDirty_ = true;
    }

    void SceneRenderCache::BeginSync(uint64_t frameIndex) {
        currentSyncFrame_ = frameIndex;
        frameStats_ = {};
        dirtySurfaceIndices_.clear();
    }

    void SceneRenderCache::Upsert(const SceneRenderObjectDesc& desc) {
        if (!desc.id.IsValid()) {
            ++frameStats_.invalidDescCount;
            return;
        }

        const auto found = indexById_.find(desc.id.value);
        if (found == indexById_.end()) {
            SceneRenderObject object{};
            object.desc = desc;
            object.valid = desc.model != nullptr;
            object.dirty = true;
            object.transformDirty = true;
            object.materialDirty = true;
            object.modelDirty = true;
            object.lastTouchedFrame = currentSyncFrame_;
            object.version = 1;

            indexById_[desc.id.value] = objects_.size();
            objects_.push_back(std::move(object));
            MarkSurfaceTopologyDirty();
            ++frameStats_.insertedCount;
            return;
        }

        SceneRenderObject& object = objects_[found->second];
        const DirtyFlags dirty = BuildDirtyFlags(object.desc, desc);
        object.lastTouchedFrame = currentSyncFrame_;
        object.valid = desc.model != nullptr;

        if (dirty.any) {
            object.desc = desc;
            object.dirty = true;
            object.transformDirty = dirty.transform;
            object.materialDirty = dirty.material;
            object.modelDirty = dirty.model;
            ++object.version;
            if (dirty.model) {
                MarkSurfaceTopologyDirty();
            } else {
                UpdateSurfaceInstancesForObject(found->second);
                if (dirty.routing) {
                    MarkSurfaceRoutingDirty();
                } else {
                    MarkSurfaceDataDirty();
                }
            }
            ++frameStats_.updatedCount;
        } else {
            object.dirty = false;
            object.transformDirty = false;
            object.materialDirty = false;
            object.modelDirty = false;
        }
    }

    void SceneRenderCache::EndSync() {
        for (size_t i = 0; i < objects_.size();) {
            if (objects_[i].lastTouchedFrame != currentSyncFrame_) {
                RemoveAt(i);
                MarkSurfaceTopologyDirty();
                ++frameStats_.removedCount;
                continue;
            }
            ++i;
        }
        RefreshStats();
    }

    void SceneRenderCache::BeginPatchSync(uint64_t frameIndex) {
        currentSyncFrame_ = frameIndex;
        frameStats_ = {};
        dirtySurfaceIndices_.clear();
    }

    void SceneRenderCache::EndPatchSync() {
        RefreshStats();
    }

    void SceneRenderCache::Remove(SceneRenderObjectId id) {
        if (!id.IsValid()) {
            return;
        }
        const auto found = indexById_.find(id.value);
        if (found == indexById_.end()) {
            return;
        }
        RemoveAt(found->second);
        ++frameStats_.removedCount;
        MarkSurfaceTopologyDirty();
        RefreshStats();
    }

    void SceneRenderCache::OnSceneLoaded() {
        Clear();
    }

    void SceneRenderCache::OnSceneUnloaded() {
        Clear();
    }

    void SceneRenderCache::MarkAllDirty() {
        for (SceneRenderObject& object : objects_) {
            object.dirty = true;
            object.transformDirty = true;
            object.materialDirty = true;
            object.modelDirty = true;
            ++object.version;
        }
        MarkSurfaceTopologyDirty();
        RefreshStats();
    }

    void SceneRenderCache::PreRenderSync() {
        if (surfaceTopologyDirty_) {
            RebuildSurfaceInstances();
            MarkAllSurfaceInstancesDirty();
            surfaceTopologyDirty_ = false;
            surfaceRoutingDirty_ = false;
            surfaceDataDirty_ = false;
            ++surfaceVersion_;
            ++surfaceRoutingVersion_;
            ++surfaceDataVersion_;
        } else {
            if (surfaceRoutingDirty_) {
                ++surfaceRoutingVersion_;
                ++surfaceDataVersion_;
                surfaceRoutingDirty_ = false;
                surfaceDataDirty_ = false;
            } else if (surfaceDataDirty_) {
                ++surfaceDataVersion_;
                surfaceDataDirty_ = false;
            }
        }
        RefreshStats();
    }

    const SceneRenderObject* SceneRenderCache::Find(SceneRenderObjectId id) const {
        if (!id.IsValid()) {
            return nullptr;
        }
        const auto found = indexById_.find(id.value);
        if (found == indexById_.end()) {
            return nullptr;
        }
        return &objects_[found->second];
    }

    const std::vector<SceneRenderObject>& SceneRenderCache::GetObjects() const {
        return objects_;
    }

    const std::vector<SceneSurfaceInstance>& SceneRenderCache::GetSurfaceInstances() const {
        return surfaceInstances_;
    }

    const std::vector<uint32_t>& SceneRenderCache::GetDirtySurfaceIndices() const {
        return dirtySurfaceIndices_;
    }

    uint64_t SceneRenderCache::GetSurfaceVersion() const {
        return surfaceVersion_;
    }

    uint64_t SceneRenderCache::GetSurfaceRoutingVersion() const {
        return surfaceRoutingVersion_;
    }

    uint64_t SceneRenderCache::GetSurfaceDataVersion() const {
        return surfaceDataVersion_;
    }

    const SceneRenderCache::Stats& SceneRenderCache::GetStats() const {
        return frameStats_;
    }

    void SceneRenderCache::RemoveAt(size_t index) {
        if (index >= objects_.size()) {
            return;
        }
        objects_.erase(objects_.begin() + static_cast<std::ptrdiff_t>(index));
        RebuildIndex();
    }

    void SceneRenderCache::RebuildIndex() {
        indexById_.clear();
        for (size_t i = 0; i < objects_.size(); ++i) {
            if (objects_[i].desc.id.IsValid()) {
                indexById_[objects_[i].desc.id.value] = i;
            }
        }
    }

    void SceneRenderCache::RebuildSurfaceInstances() {
        surfaceInstances_.clear();

        uint64_t reserveCount = 0;
        for (const SceneRenderObject& object : objects_) {
            if (object.desc.renderModel != nullptr && object.desc.renderModel->valid) {
                reserveCount += object.desc.renderModel->surfaces.size();
            }
        }
        surfaceInstances_.reserve(static_cast<size_t>(
            (std::min)(reserveCount, static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))));

        for (size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex) {
            const SceneRenderObject& object = objects_[objectIndex];
            if (!IsSurfaceSourceUsable(object)) {
                continue;
            }

            const RenderModelAsset& renderModel = *object.desc.renderModel;
            const std::vector<MATH::Mat4> nodeGlobals = BuildObjectNodeGlobals(object);
            for (size_t surfaceIndex = 0; surfaceIndex < renderModel.surfaces.size(); ++surfaceIndex) {
                const RenderSurfaceRecord& surface = renderModel.surfaces[surfaceIndex];
                surfaceInstances_.push_back(BuildSurfaceInstance(
                    object,
                    NUMERIC::SaturateToUint32(objectIndex),
                    surface,
                    NUMERIC::SaturateToUint32(surfaceIndex),
                    nodeGlobals));
            }
        }
    }

    void SceneRenderCache::UpdateSurfaceInstancesForObject(size_t objectIndex) {
        if (surfaceTopologyDirty_ || objectIndex >= objects_.size()) {
            return;
        }

        const SceneRenderObject& object = objects_[objectIndex];
        if (!IsSurfaceSourceUsable(object)) {
            MarkSurfaceTopologyDirty();
            return;
        }

        const RenderModelAsset& renderModel = *object.desc.renderModel;
        const std::vector<MATH::Mat4> nodeGlobals = BuildObjectNodeGlobals(object);
        uint32_t updatedSurfaceCount = 0;
        for (size_t surfaceInstanceIndex = 0;
            surfaceInstanceIndex < surfaceInstances_.size();
            ++surfaceInstanceIndex) {

            SceneSurfaceInstance& surfaceInstance = surfaceInstances_[surfaceInstanceIndex];
            if (surfaceInstance.objectIndex != NUMERIC::SaturateToUint32(objectIndex)) {
                continue;
            }
            if (surfaceInstance.surfaceIndex >= renderModel.surfaces.size()) {
                MarkSurfaceTopologyDirty();
                return;
            }

            surfaceInstance = BuildSurfaceInstance(
                object,
                NUMERIC::SaturateToUint32(objectIndex),
                renderModel.surfaces[surfaceInstance.surfaceIndex],
                surfaceInstance.surfaceIndex,
                nodeGlobals);
            MarkDirtySurfaceIndex(NUMERIC::SaturateToUint32(surfaceInstanceIndex));
            ++updatedSurfaceCount;
        }

        if (updatedSurfaceCount == 0 && !renderModel.surfaces.empty()) {
            MarkSurfaceTopologyDirty();
        }
    }

    void SceneRenderCache::MarkSurfaceTopologyDirty() {
        surfaceTopologyDirty_ = true;
        surfaceRoutingDirty_ = true;
        surfaceDataDirty_ = true;
    }

    void SceneRenderCache::MarkSurfaceRoutingDirty() {
        if (surfaceTopologyDirty_) {
            return;
        }
        surfaceRoutingDirty_ = true;
        surfaceDataDirty_ = true;
    }

    void SceneRenderCache::MarkSurfaceDataDirty() {
        if (surfaceTopologyDirty_ || surfaceRoutingDirty_) {
            return;
        }
        surfaceDataDirty_ = true;
    }

    void SceneRenderCache::MarkDirtySurfaceIndex(uint32_t surfaceIndex) {
        if (surfaceIndex >= surfaceInstances_.size()) {
            return;
        }
        if (std::find(
            dirtySurfaceIndices_.begin(),
            dirtySurfaceIndices_.end(),
            surfaceIndex) != dirtySurfaceIndices_.end()) {
            return;
        }
        dirtySurfaceIndices_.push_back(surfaceIndex);
    }

    void SceneRenderCache::MarkAllSurfaceInstancesDirty() {
        dirtySurfaceIndices_.clear();
        dirtySurfaceIndices_.reserve(surfaceInstances_.size());
        for (uint32_t surfaceIndex = 0;
            surfaceIndex < surfaceInstances_.size();
            ++surfaceIndex) {
            dirtySurfaceIndices_.push_back(surfaceIndex);
        }
    }

    void SceneRenderCache::RefreshStats() {
        const uint32_t insertedCount = frameStats_.insertedCount;
        const uint32_t updatedCount = frameStats_.updatedCount;
        const uint32_t removedCount = frameStats_.removedCount;
        const uint32_t invalidSyncDescCount = frameStats_.invalidDescCount;

        Stats stats{};
        stats.insertedCount = insertedCount;
        stats.updatedCount = updatedCount;
        stats.removedCount = removedCount;
        stats.invalidDescCount = invalidSyncDescCount;

        for (const SceneRenderObject& object : objects_) {
            ++stats.renderObjectCount;
            if (object.desc.visible) {
                ++stats.visibleObjectCount;
            } else {
                ++stats.hiddenObjectCount;
            }
            if (object.dirty) {
                ++stats.dirtyObjectCount;
            }
            if (object.desc.isStatic) {
                ++stats.staticObjectCount;
            } else {
                ++stats.dynamicObjectCount;
            }
            if (object.desc.renderModel != nullptr && object.desc.renderModel->valid) {
                ++stats.renderModelValidCount;
            } else {
                ++stats.renderModelInvalidCount;
            }
            if (IsInvalidStoredDesc(object.desc)) {
                ++stats.invalidDescCount;
            }
        }

        stats.surfaceInstanceCount = NUMERIC::SaturateToUint32(surfaceInstances_.size());
        stats.dirtySurfaceInstanceCount = NUMERIC::SaturateToUint32(dirtySurfaceIndices_.size());
        for (const SceneSurfaceInstance& instance : surfaceInstances_) {
            if (instance.visible) {
                ++stats.visibleSurfaceInstanceCount;
            } else {
                ++stats.hiddenSurfaceInstanceCount;
            }
            if (instance.isStatic) {
                ++stats.staticSurfaceInstanceCount;
            } else {
                ++stats.dynamicSurfaceInstanceCount;
            }
            if (instance.skinned) {
                ++stats.skinnedSurfaceInstanceCount;
            } else {
                ++stats.staticGeometrySurfaceInstanceCount;
            }
            if (!instance.clusteredGeometryPath.empty()) {
                ++stats.clusteredGeometrySurfaceInstanceCount;
            }
            if (!instance.valid) {
                ++stats.invalidSurfaceInstanceCount;
            }
            if (!instance.hasDrawWorldMatrix) {
                ++stats.missingSurfaceMatrixCount;
            }
            if (!BOUNDS::IsUsable(instance.worldBounds)) {
                ++stats.invalidSurfaceBoundsCount;
            }
        }

        frameStats_ = stats;
    }

}
