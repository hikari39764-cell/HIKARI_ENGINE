#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

#include <algorithm>
#include <atomic>
#include <limits>

#include "Core/Numeric/HIKARI_IntegerConversion.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceResolver.h"
#include "Render3D/Runtime/HIKARI_SceneSurfaceFloatPool.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        std::atomic<uint64_t> gNextObjectLifetimeGeneration{ 1u };

        uint64_t AllocateObjectLifetimeGeneration() noexcept {
            uint64_t generation = gNextObjectLifetimeGeneration.fetch_add(
                1u,
                std::memory_order_relaxed);
            if (generation == 0u) {
                generation = gNextObjectLifetimeGeneration.fetch_add(
                    1u,
                    std::memory_order_relaxed);
            }
            return generation;
        }

        bool EqualVec3(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        bool EqualVec4(const MATH::Vec4& lhs, const MATH::Vec4& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y &&
                lhs.z == rhs.z && lhs.w == rhs.w;
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

        bool EqualMaterialOverrides(
            const std::vector<SceneRenderMaterialOverride>& lhs,
            const std::vector<SceneRenderMaterialOverride>& rhs) {

            if (lhs.size() != rhs.size()) {
                return false;
            }
            for (size_t index = 0; index < lhs.size(); ++index) {
                if (lhs[index].materialIndex != rhs[index].materialIndex ||
                    lhs[index].material != rhs[index].material ||
                    lhs[index].materialRevision !=
                        rhs[index].materialRevision) {

                    return false;
                }
            }
            return true;
        }

        const SceneRenderMaterialOverride* FindMaterialOverride(
            const SceneRenderObjectDesc& desc,
            uint32_t materialIndex) {

            const auto found = std::find_if(
                desc.materialOverrides.begin(),
                desc.materialOverrides.end(),
                [materialIndex](
                    const SceneRenderMaterialOverride& overrideSlot) {
                    return
                        overrideSlot.materialIndex == materialIndex &&
                        overrideSlot.material != nullptr;
                });
            if (found == desc.materialOverrides.end()) {
                return nullptr;
            }
            return &*found;
        }

        struct DirtyFlags {
            bool any = false;
            bool transform = false;
            bool material = false;
            bool model = false;
            bool routing = false;
            bool surfaceData = false;
        };

        // Float-pool offsets and the effective terrain dimensions are derived
        // at PreRenderSync time.  A transform-only cache update must carry
        // those values across the surface-instance rebuild; otherwise the
        // rebuilt instance points back at the neutral tile while the pool is
        // intentionally not repacked.
        using PackedSurfaceMetadata = std::array<uint32_t, 10u>;

        PackedSurfaceMetadata CapturePackedSurfaceMetadata(
            const SceneSurfaceInstance& instance) {

            return {
                instance.sandSurfaceData.gridResolution,
                instance.sandSurfaceData.layerCount,
                instance.sandSurfaceHeightOffset,
                instance.sandSurfaceDeformationOffset,
                instance.sandSurfaceLayerWeightsOffset,
                instance.sandSurfaceCoverageOffset,
                instance.sandSurfaceLayerTintsOffset,
                instance.waterSurfaceData.coverageResolution,
                instance.waterSurfaceLiquidCoverageOffset,
                instance.sandSurfaceCoreResident ? 1u : 0u,
            };
        }

        void RestorePackedSurfaceMetadata(
            SceneSurfaceInstance& instance,
            const PackedSurfaceMetadata& metadata) {

            instance.sandSurfaceData.gridResolution = metadata[0];
            instance.sandSurfaceData.layerCount = metadata[1];
            instance.sandSurfaceHeightOffset = metadata[2];
            instance.sandSurfaceDeformationOffset = metadata[3];
            instance.sandSurfaceLayerWeightsOffset = metadata[4];
            instance.sandSurfaceCoverageOffset = metadata[5];
            instance.sandSurfaceLayerTintsOffset = metadata[6];
            instance.waterSurfaceData.coverageResolution = metadata[7];
            instance.waterSurfaceLiquidCoverageOffset = metadata[8];
            instance.sandSurfaceCoreResident = metadata[9] != 0u;
        }

        DirtyFlags BuildDirtyFlags(const SceneRenderObjectDesc& oldDesc, const SceneRenderObjectDesc& newDesc) {
            DirtyFlags flags{};

            flags.model =
                oldDesc.model != newDesc.model ||
                oldDesc.renderModel != newDesc.renderModel ||
                oldDesc.clusteredGeometryPath != newDesc.clusteredGeometryPath ||
                !EqualBounds(oldDesc.localBounds, newDesc.localBounds);

            flags.transform =
                !EqualTransform(oldDesc.worldTransform, newDesc.worldTransform) ||
                !EqualBounds(oldDesc.worldBounds, newDesc.worldBounds) ||
                !EqualVec3(
                    oldDesc.worldBoundsPadding,
                    newDesc.worldBoundsPadding);

            flags.material =
                !EqualMaterialOverrides(
                    oldDesc.materialOverrides,
                    newDesc.materialOverrides) ||
                oldDesc.editorSelectionMaterialIndex !=
                    newDesc.editorSelectionMaterialIndex ||
                oldDesc.materialFxProfileId != newDesc.materialFxProfileId ||
                oldDesc.postGroupMask != newDesc.postGroupMask ||
                oldDesc.materialFxValuesInitialized != newDesc.materialFxValuesInitialized ||
                !EqualMaterialFxValues(oldDesc.materialFxParamValues, newDesc.materialFxParamValues) ||
                oldDesc.foliageWind != newDesc.foliageWind ||
                !EqualVec4(
                    oldDesc.foliageWindParams,
                    newDesc.foliageWindParams) ||
                oldDesc.waterSurface != newDesc.waterSurface ||
                oldDesc.sandSurface != newDesc.sandSurface;

            // Feature enable/disable changes routing, but per-frame terrain payload
            // changes only require the existing forward-data patch path.
            flags.surfaceData =
                oldDesc.sandSurfaceProjectionRevision !=
                    newDesc.sandSurfaceProjectionRevision ||
                oldDesc.sandSurfaceHeights !=
                    newDesc.sandSurfaceHeights ||
                oldDesc.sandSurfaceDeformations !=
                    newDesc.sandSurfaceDeformations ||
                oldDesc.sandSurfaceLayerWeights !=
                    newDesc.sandSurfaceLayerWeights ||
                oldDesc.sandSurfaceCoverage !=
                    newDesc.sandSurfaceCoverage ||
                !WATER::WaterSurfaceRenderDataEqual(
                    oldDesc.waterSurfaceData,
                    newDesc.waterSurfaceData) ||
                !SAND::SandSurfaceRenderDataEqual(
                    oldDesc.sandSurfaceData,
                    newDesc.sandSurfaceData);

            const bool animationDataChanged =
                oldDesc.animationPoseRevision !=
                    newDesc.animationPoseRevision ||
                oldDesc.animationPose != newDesc.animationPose;

            flags.routing =
                flags.material ||
                oldDesc.visible != newDesc.visible ||
                oldDesc.nodeVisibilityMask !=
                    newDesc.nodeVisibilityMask ||
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
                flags.surfaceData ||
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
            const std::vector<MATH::Mat4>& nodeGlobals,
            const std::vector<MATH::Mat4>& bindNodeGlobals) {

            SceneSurfaceInstance instance{};
            instance.objectId = object.desc.id;
            instance.objectLifetimeGeneration =
                object.lifetimeGeneration;
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
            const bool nodeVisible =
                surface.nodeIndex == kInvalidRenderSurfaceIndex ||
                surface.nodeIndex >= object.desc.nodeVisibilityMask.size() ||
                object.desc.nodeVisibilityMask[surface.nodeIndex] != 0u;
            instance.visible = object.desc.visible && nodeVisible;
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
            const SceneRenderMaterialOverride* materialOverride =
                FindMaterialOverride(
                    object.desc,
                    surface.materialIndex);
            if (materialOverride != nullptr) {
                instance.materialOverride = materialOverride->material;
                instance.materialOverrideRevision =
                    materialOverride->materialRevision;
            }
            instance.editorSelectionOutline =
                object.desc.editorSelectionMaterialIndex ==
                surface.materialIndex;
            instance.materialFxProfileId = object.desc.materialFxProfileId;
            instance.postGroupMask = object.desc.postGroupMask;
            instance.materialFxValuesInitialized = object.desc.materialFxValuesInitialized;
            CopyMaterialFxValues(object.desc, instance);
            instance.foliageWind = object.desc.foliageWind;
            instance.foliageWindParams = object.desc.foliageWindParams;
            instance.waterSurface = object.desc.waterSurface;
            instance.waterSurfaceData = object.desc.waterSurfaceData;
            instance.sandSurface = object.desc.sandSurface;
            instance.sandSurfaceCoreResident = false;
            instance.sandSurfaceData = object.desc.sandSurfaceData;
            instance.sandSurfaceHeights = object.desc.sandSurfaceHeights;
            instance.sandSurfaceDeformations =
                object.desc.sandSurfaceDeformations;
            instance.sandSurfaceLayerWeights =
                object.desc.sandSurfaceLayerWeights;
            instance.sandSurfaceCoverage =
                object.desc.sandSurfaceCoverage;
            instance.sandSurfaceProjectionRevision =
                object.desc.sandSurfaceProjectionRevision;

            instance.hasDrawWorldMatrix = ResolveRenderSurfaceDrawWorldMatrix(
                object.desc.worldTransform,
                surface,
                nodeGlobals,
                instance.drawWorldMatrix);
            const bool hasClusterWorldMatrix =
                ResolveClusterSurfaceWorldMatrix(
                    object.desc.worldTransform,
                    surface,
                    nodeGlobals,
                    bindNodeGlobals,
                    object.desc.hasRuntimeAnimation,
                    instance.clusterWorldMatrix);
            instance.worldBounds = ResolveRenderSurfaceWorldBounds(
                object.desc.worldBounds,
                surface,
                instance.drawWorldMatrix,
                instance.hasDrawWorldMatrix);
            if (BOUNDS::IsUsable(instance.worldBounds)) {
                const MATH::Vec3 requestedPadding =
                    MATH::IsFinite(object.desc.worldBoundsPadding)
                        ? object.desc.worldBoundsPadding
                        : MATH::Vec3{};
                const MATH::Vec3 padding{
                    (std::max)(requestedPadding.x, 0.0f),
                    (std::max)(requestedPadding.y, 0.0f),
                    (std::max)(requestedPadding.z, 0.0f),
                };
                instance.worldBounds.min.x -= padding.x;
                instance.worldBounds.min.y -= padding.y;
                instance.worldBounds.min.z -= padding.z;
                instance.worldBounds.max.x += padding.x;
                instance.worldBounds.max.y += padding.y;
                instance.worldBounds.max.z += padding.z;
            }

            instance.valid =
                IsSurfaceSourceUsable(object) &&
                HasValidRenderSurfacePrimitive(object.desc.model, surface) &&
                instance.hasDrawWorldMatrix &&
                hasClusterWorldMatrix &&
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
        surfaceFloatPoolVersion_ = 0;
        surfaceTopologyDirty_ = true;
        surfaceRoutingDirty_ = true;
        surfaceDataDirty_ = true;
        surfaceFloatPoolDirty_ = true;
        sandSurfaceHeights_.clear();
        previousPackedSurfaceScratch_.clear();
        sourceSandGridResolutionScratch_.clear();
        terrainCoreResidentScratch_.clear();
        surfaceTintScratch_.clear();
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
            object.lifetimeGeneration =
                AllocateObjectLifetimeGeneration();
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
                    surfaceFloatPoolDirty_ =
                        surfaceFloatPoolDirty_ || dirty.surfaceData;
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
        const bool repackSurfaceFloatPool = surfaceFloatPoolDirty_;
        const uint64_t dataVersionBeforePacking = surfaceDataVersion_;
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

        if (!repackSurfaceFloatPool) {
            RefreshStats();
            return;
        }

        previousPackedSurfaceScratch_.resize(surfaceInstances_.size());
        sourceSandGridResolutionScratch_.assign(
            surfaceInstances_.size(), 1u);
        terrainCoreResidentScratch_.assign(
            surfaceInstances_.size(), uint8_t{ 0u });
        for (uint32_t surfaceIndex = 0u;
            surfaceIndex < surfaceInstances_.size();
            ++surfaceIndex) {

            SceneSurfaceInstance& instance = surfaceInstances_[surfaceIndex];
            previousPackedSurfaceScratch_[surfaceIndex] =
                CapturePackedSurfaceMetadata(instance);

            // 前フレームの縮退結果を authoring/runtime source へ逆流させない。
            if (instance.objectIndex < objects_.size()) {
                const SceneRenderObjectDesc& source =
                    objects_[instance.objectIndex].desc;
                instance.waterSurfaceData = source.waterSurfaceData;
                instance.sandSurfaceData = source.sandSurfaceData;
                instance.sandSurfaceHeights = source.sandSurfaceHeights;
                instance.sandSurfaceDeformations =
                    source.sandSurfaceDeformations;
                instance.sandSurfaceLayerWeights =
                    source.sandSurfaceLayerWeights;
                instance.sandSurfaceCoverage =
                    source.sandSurfaceCoverage;
                instance.sandSurfaceProjectionRevision =
                    source.sandSurfaceProjectionRevision;
            }
            sourceSandGridResolutionScratch_[surfaceIndex] =
                (std::max)(instance.sandSurfaceData.gridResolution, 1u);
            instance.sandSurfaceHeightOffset = 0u;
            instance.sandSurfaceDeformationOffset = 0u;
            instance.sandSurfaceLayerWeightsOffset = 0u;
            instance.sandSurfaceCoverageOffset = 0u;
            instance.sandSurfaceLayerTintsOffset = 0u;
            instance.waterSurfaceLiquidCoverageOffset = 0u;
            instance.sandSurfaceCoreResident = false;
        }

        // 全 surface と water が共有する唯一の float pool を、surface 順の
        // deterministic first-fit で構築する。terrain の height/weight/coverage
        // は atomic に入り、必要なら同じ範囲を保ったまま格子だけを縮小する。
        // 最小格子さえ収容できない surface は明示的に non-resident とし、
        // neutral tile を「平らな砂地」として公開しない。
        SceneSurfaceFloatPoolPacker floatPool(
            sandSurfaceHeights_,
            SAND::kSandSurfaceMaxHeightCells);
        for (uint32_t surfaceIndex = 0u;
            surfaceIndex < surfaceInstances_.size();
            ++surfaceIndex) {

            SceneSurfaceInstance& instance = surfaceInstances_[surfaceIndex];
            if (!instance.sandSurface) {
                continue;
            }
            const SceneSurfaceTerrainFloatAllocation allocation =
                floatPool.PackTerrainCore({
                    sourceSandGridResolutionScratch_[surfaceIndex],
                    instance.sandSurfaceData.layerCount,
                    instance.sandSurfaceHeights.get(),
                    instance.sandSurfaceLayerWeights
                        ? instance.sandSurfaceLayerWeights.get()
                        : &instance.sandSurfaceData.layerWeights,
                    instance.sandSurfaceCoverage
                        ? instance.sandSurfaceCoverage.get()
                        : &instance.sandSurfaceData.coverage,
                });
            terrainCoreResidentScratch_[surfaceIndex] =
                allocation.resident ? uint8_t{ 1u } : uint8_t{ 0u };
            if (!allocation.resident) {
                continue;
            }
            instance.sandSurfaceCoreResident = true;
            instance.sandSurfaceData.gridResolution =
                allocation.gridResolution;
            instance.sandSurfaceData.layerCount = allocation.layerCount;
            instance.sandSurfaceHeightOffset = allocation.heightOffset;
            instance.sandSurfaceLayerWeightsOffset =
                allocation.layerWeightsOffset;
            instance.sandSurfaceCoverageOffset = allocation.coverageOffset;
        }

        // water coverage も同じ残量から正方形 field を確保する。
        // 入らない場合の offset 0 は shader 契約上「全面 water」の安全 fallback。
        for (SceneSurfaceInstance& instance : surfaceInstances_) {
            if (!instance.waterSurface ||
                instance.waterSurfaceData.liquidCoverage.empty()) {
                continue;
            }
            floatPool.TryPackSquareField(
                instance.waterSurfaceData.liquidCoverage,
                instance.waterSurfaceData.coverageResolution,
                instance.waterSurfaceLiquidCoverageOffset);
        }

        // runtime deformation は total height に既に含まれる optional shading
        // field。core と water coverage を優先し、残量不足時は offset 0 へ落とす。
        for (uint32_t surfaceIndex = 0u;
            surfaceIndex < surfaceInstances_.size();
            ++surfaceIndex) {

            SceneSurfaceInstance& instance = surfaceInstances_[surfaceIndex];
            if (!instance.sandSurface ||
                terrainCoreResidentScratch_[surfaceIndex] == 0u ||
                !instance.sandSurfaceDeformations ||
                instance.sandSurfaceDeformations->empty()) {
                continue;
            }
            floatPool.TryPackSquareFieldResampled(
                *instance.sandSurfaceDeformations,
                sourceSandGridResolutionScratch_[surfaceIndex],
                instance.sandSurfaceData.gridResolution,
                instance.sandSurfaceDeformationOffset);
        }

        // Layer style is optional float-pool payload. Offset 0 remains a valid
        // legacy sand fallback, so it is packed after geometry/coverage fields.
        for (SceneSurfaceInstance& instance : surfaceInstances_) {
            if (!instance.sandSurface ||
                !instance.sandSurfaceCoreResident) {
                continue;
            }
            const uint32_t layerCount = instance.sandSurfaceData.layerCount;
            const auto& styles = instance.sandSurfaceData.layerStyles;
            const MATH::Vec3 baseLight = instance.sandSurfaceData.lightTint;
            const MATH::Vec3 baseDark = instance.sandSurfaceData.darkTint;
            surfaceTintScratch_.clear();
            surfaceTintScratch_.reserve(
                static_cast<std::size_t>(layerCount) *
                    SAND::kSandSurfaceLayerStyleFloatCount);
            for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                SAND::SandSurfaceLayerRenderStyle style =
                    layer < styles.size()
                        ? styles[layer]
                        : SAND::SandSurfaceLayerRenderStyle{};
                if (layer >= styles.size() && layer == 0u) {
                    style.lightTint = baseLight;
                    style.darkTint = baseDark;
                }
                surfaceTintScratch_.insert(
                    surfaceTintScratch_.end(),
                    {
                        style.lightTint.x,
                        style.lightTint.y,
                        style.lightTint.z,
                        style.textureStrength,
                        style.darkTint.x,
                        style.darkTint.y,
                        style.darkTint.z,
                        style.normalStrength,
                        style.atlasScaleBias.x,
                        style.atlasScaleBias.y,
                        style.atlasScaleBias.z,
                        style.atlasScaleBias.w,
                        style.tilesPerMeter,
                        style.roughness,
                        style.saturation,
                        static_cast<float>(style.materialKind),
                        style.shadowStrength,
                        style.granularStrength,
                        style.facetSizeMeters,
                        style.facetStrength,
                    });
            }
            floatPool.TryPackLinear(
                std::span<const float>(surfaceTintScratch_),
                instance.sandSurfaceLayerTintsOffset);
        }

        // 先行 surface のサイズ変化は後続 offset も動かす。影響を受けた record
        // を明示的に data-dirty にし、routing rebuild なしで全 offset を patch する。
        bool packedStateChanged = false;
        for (uint32_t surfaceIndex = 0u;
            surfaceIndex < surfaceInstances_.size();
            ++surfaceIndex) {

            const PackedSurfaceMetadata current =
                CapturePackedSurfaceMetadata(surfaceInstances_[surfaceIndex]);
            if (previousPackedSurfaceScratch_[surfaceIndex] ==
                current) {
                continue;
            }
            MarkDirtySurfaceIndex(surfaceIndex);
            packedStateChanged = true;
        }
        if (packedStateChanged && surfaceDataVersion_ == dataVersionBeforePacking) {
            ++surfaceDataVersion_;
        }
        ++surfaceFloatPoolVersion_;
        if (surfaceFloatPoolVersion_ == 0u) {
            ++surfaceFloatPoolVersion_;
        }
        surfaceFloatPoolDirty_ = false;
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

    const std::vector<float>* SceneRenderCache::GetSandSurfaceHeights() const {
        return &sandSurfaceHeights_;
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

    uint64_t SceneRenderCache::GetSurfaceFloatPoolVersion() const {
        return surfaceFloatPoolVersion_;
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
            const std::vector<MATH::Mat4>& bindNodeGlobals =
                object.desc.hasRuntimeAnimation
                ? renderModel.bindNodeGlobals
                : nodeGlobals;
            for (size_t surfaceIndex = 0; surfaceIndex < renderModel.surfaces.size(); ++surfaceIndex) {
                const RenderSurfaceRecord& surface = renderModel.surfaces[surfaceIndex];
                surfaceInstances_.push_back(BuildSurfaceInstance(
                    object,
                    NUMERIC::SaturateToUint32(objectIndex),
                    surface,
                    NUMERIC::SaturateToUint32(surfaceIndex),
                    nodeGlobals,
                    bindNodeGlobals));
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
        const std::vector<MATH::Mat4>& bindNodeGlobals =
            object.desc.hasRuntimeAnimation
            ? renderModel.bindNodeGlobals
            : nodeGlobals;
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

            const PackedSurfaceMetadata packedMetadata =
                CapturePackedSurfaceMetadata(surfaceInstance);
            SceneSurfaceInstance updatedSurfaceInstance = BuildSurfaceInstance(
                object,
                NUMERIC::SaturateToUint32(objectIndex),
                renderModel.surfaces[surfaceInstance.surfaceIndex],
                surfaceInstance.surfaceIndex,
                nodeGlobals,
                bindNodeGlobals);
            RestorePackedSurfaceMetadata(
                updatedSurfaceInstance,
                packedMetadata);
            surfaceInstance = std::move(updatedSurfaceInstance);
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
        surfaceFloatPoolDirty_ = true;
    }

    void SceneRenderCache::MarkSurfaceRoutingDirty() {
        if (surfaceTopologyDirty_) {
            return;
        }
        surfaceRoutingDirty_ = true;
        surfaceDataDirty_ = true;
        surfaceFloatPoolDirty_ = true;
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
