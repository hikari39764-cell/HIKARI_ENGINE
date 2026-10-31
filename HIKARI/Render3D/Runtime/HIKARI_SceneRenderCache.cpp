#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "Core/Numeric/HIKARI_IntegerConversion.h"
#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceResolver.h"

namespace HIKARI::RENDER3D::RUNTIME {

namespace {
// 診断（環境変数 HIKARI_GPUSCENE_TRACE）：GPU のシーンを組み直させる変化（物の出入り、モデル・経路の変化）を、
// 同期ごとに 6 件まで記録する（どの物が作り直しを起こしているかを探す）。
void TraceSceneChange(const char* what, const SceneRenderObjectDesc& desc, uint64_t frame) {
    static const bool enabled = std::getenv("HIKARI_GPUSCENE_TRACE") != nullptr;
    static uint64_t lastFrame = ~0ull;
    static uint32_t count = 0u;
    if (!enabled) {
        return;
    }
    if (frame != lastFrame) {
        lastFrame = frame;
        count = 0u;
    }
    if (++count > 6u) {
        return;
    }
    std::fprintf(stderr, "[SceneCache] frame=%llu %s id=%llu model=%s visible=%d\n", static_cast<unsigned long long>(frame),
                 what, static_cast<unsigned long long>(desc.id.value),
                 desc.model != nullptr ? desc.model->sourcePath.c_str() : "-", desc.visible ? 1 : 0);
}
std::atomic<uint64_t> gNextObjectLifetimeGeneration{1u};

// float pool の version は全 cache で一意にする。GPU slot は version だけで内容を
// 照合するため、別 cache の同じ番号を「最新」と取り違えない。
std::atomic<uint64_t> gNextSurfaceFloatPoolVersion{1u};

uint64_t AllocateSurfaceFloatPoolVersion() noexcept {
    uint64_t version = gNextSurfaceFloatPoolVersion.fetch_add(1u, std::memory_order_relaxed);
    if (version == 0u) {
        version = gNextSurfaceFloatPoolVersion.fetch_add(1u, std::memory_order_relaxed);
    }
    return version;
}

uint64_t AllocateObjectLifetimeGeneration() noexcept {
    uint64_t generation = gNextObjectLifetimeGeneration.fetch_add(1u, std::memory_order_relaxed);
    if (generation == 0u) {
        generation = gNextObjectLifetimeGeneration.fetch_add(1u, std::memory_order_relaxed);
    }
    return generation;
}

bool EqualMaterialFxValues(const DirectX::XMFLOAT4* lhs, const DirectX::XMFLOAT4* rhs) {
    for (int i = 0; i < VFX::kMaterialFxUserCount; ++i) {
        if (lhs[i].x != rhs[i].x || lhs[i].y != rhs[i].y || lhs[i].z != rhs[i].z || lhs[i].w != rhs[i].w) {
            return false;
        }
    }
    return true;
}

bool EqualMaterialOverrides(const std::vector<SceneRenderMaterialOverride>& lhs,
                            const std::vector<SceneRenderMaterialOverride>& rhs, bool compareRevisions = true) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].materialIndex != rhs[index].materialIndex || lhs[index].material != rhs[index].material ||
            (compareRevisions && lhs[index].materialRevision != rhs[index].materialRevision)) {
            return false;
        }
    }
    return true;
}

const SceneRenderMaterialOverride* FindMaterialOverride(const SceneRenderObjectDesc& desc, uint32_t materialIndex) {
    const auto found =
        std::find_if(desc.materialOverrides.begin(), desc.materialOverrides.end(),
                     [materialIndex](const SceneRenderMaterialOverride& overrideSlot) {
                         return overrideSlot.materialIndex == materialIndex && overrideSlot.material != nullptr;
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

DirtyFlags BuildDirtyFlags(const SceneRenderObjectDesc& oldDesc, const SceneRenderObjectDesc& newDesc) {
    DirtyFlags flags{};

    flags.model = oldDesc.model != newDesc.model || oldDesc.renderModel != newDesc.renderModel ||
                  oldDesc.clusteredGeometryPath != newDesc.clusteredGeometryPath ||
                  oldDesc.localBounds != newDesc.localBounds;

    flags.transform = !oldDesc.worldTransform.HasSameLocalValue(newDesc.worldTransform) ||
                      oldDesc.worldTransform.GetParent() != newDesc.worldTransform.GetParent() ||
                      oldDesc.worldBounds != newDesc.worldBounds ||
                      oldDesc.worldBoundsPadding != newDesc.worldBoundsPadding;

    flags.material =
        !EqualMaterialOverrides(oldDesc.materialOverrides, newDesc.materialOverrides) ||
        oldDesc.editorSelectionMaterialIndex != newDesc.editorSelectionMaterialIndex ||
        oldDesc.materialFxProfileId != newDesc.materialFxProfileId || oldDesc.postGroupMask != newDesc.postGroupMask ||
        oldDesc.materialFxValuesInitialized != newDesc.materialFxValuesInitialized ||
        !EqualMaterialFxValues(oldDesc.materialFxParamValues, newDesc.materialFxParamValues) ||
        oldDesc.foliageWind != newDesc.foliageWind || oldDesc.foliageWindParams != newDesc.foliageWindParams ||
        oldDesc.waterSurface != newDesc.waterSurface || oldDesc.terrainSurface != newDesc.terrainSurface;

    // Feature enable/disable changes routing, but per-frame terrain payload
    // changes only require the existing forward-data patch path.
    flags.surfaceData = oldDesc.terrainSurfaceProjectionRevision != newDesc.terrainSurfaceProjectionRevision ||
                        oldDesc.terrainSurfaceFields != newDesc.terrainSurfaceFields ||
                        !WATER::WaterSurfaceRenderDataEqual(oldDesc.waterSurfaceData, newDesc.waterSurfaceData) ||
                        !TERRAIN::TerrainSurfaceRenderDataEqual(oldDesc.terrainSurfaceData, newDesc.terrainSurfaceData);

    const bool animationDataChanged = oldDesc.animationPoseRevision != newDesc.animationPoseRevision ||
                                      oldDesc.animationPose != newDesc.animationPose;

    flags.routing = !EqualMaterialOverrides(oldDesc.materialOverrides, newDesc.materialOverrides, false) ||
                    oldDesc.materialFxProfileId != newDesc.materialFxProfileId ||
                    oldDesc.foliageWind != newDesc.foliageWind || oldDesc.waterSurface != newDesc.waterSurface ||
                    oldDesc.terrainSurface != newDesc.terrainSurface || oldDesc.visible != newDesc.visible ||
                    oldDesc.nodeVisibilityMask != newDesc.nodeVisibilityMask || oldDesc.isStatic != newDesc.isStatic ||
                    oldDesc.castShadow != newDesc.castShadow || oldDesc.receiveShadow != newDesc.receiveShadow ||
                    oldDesc.hasRuntimeAnimation != newDesc.hasRuntimeAnimation ||
                    oldDesc.hasSpecialRenderDebug != newDesc.hasSpecialRenderDebug ||
                    oldDesc.allowStaticCachedForward != newDesc.allowStaticCachedForward;

    flags.any =
        flags.model || flags.transform || flags.material || flags.routing || flags.surfaceData || animationDataChanged;

    return flags;
}

// 地形 field の値だけが進んだ更新。格子・style・offset 配置は変わらないので、
// float pool の該当行を同じ場所へ書き戻せる。
bool IsTerrainContentOnlyUpdate(const SceneRenderObjectDesc& oldDesc, const SceneRenderObjectDesc& newDesc,
                                const DirtyFlags& flags) {
    return flags.surfaceData && !flags.model && !flags.routing && !flags.material && newDesc.terrainSurface &&
           newDesc.terrainSurfaceFields != nullptr && oldDesc.terrainSurfaceFields == newDesc.terrainSurfaceFields &&
           newDesc.terrainSurfaceProjectionRevision != oldDesc.terrainSurfaceProjectionRevision &&
           newDesc.terrainSurfaceDirtyBaseRevision == oldDesc.terrainSurfaceProjectionRevision &&
           TERRAIN::TerrainSurfaceRenderDataEqual(oldDesc.terrainSurfaceData, newDesc.terrainSurfaceData) &&
           WATER::WaterSurfaceRenderDataEqual(oldDesc.waterSurfaceData, newDesc.waterSurfaceData);
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
    return object.valid && object.desc.id.IsValid() && object.desc.model != nullptr &&
           object.desc.renderModel != nullptr && object.desc.renderModel->valid;
}

std::vector<MATH::Mat4> BuildObjectNodeGlobals(const SceneRenderObject& object) {
    const RenderModelAsset& renderModel = *object.desc.renderModel;
    if (!object.desc.hasRuntimeAnimation) {
        return BuildRenderModelNodeGlobals(renderModel);
    }

    return BuildRenderModelNodeGlobals(renderModel, object.desc.model, object.desc.animationPose->localPose);
}

SceneSurfaceInstance BuildSurfaceInstance(const SceneRenderObject& object, uint32_t objectIndex,
                                          const RenderSurfaceRecord& surface, uint32_t surfaceIndex,
                                          const std::vector<MATH::Mat4>& nodeGlobals,
                                          const std::vector<MATH::Mat4>& bindNodeGlobals) {
    SceneSurfaceInstance instance{};
    instance.objectId = object.desc.id;
    instance.objectLifetimeGeneration = object.lifetimeGeneration;
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
    const bool nodeVisible = surface.nodeIndex == kInvalidRenderSurfaceIndex ||
                             surface.nodeIndex >= object.desc.nodeVisibilityMask.size() ||
                             object.desc.nodeVisibilityMask[surface.nodeIndex] != 0u;
    instance.visible = object.desc.visible && nodeVisible;
    instance.isStatic = object.desc.isStatic;
    instance.castShadow = object.desc.castShadow && surface.castShadowDefault;
    instance.receiveShadow = object.desc.receiveShadow && surface.receiveShadowDefault;
    instance.hasRuntimeAnimation = object.desc.hasRuntimeAnimation;
    instance.animationPose = object.desc.animationPose;
    instance.animationPoseRevision = object.desc.animationPoseRevision;
    instance.hasSpecialRenderDebug = object.desc.hasSpecialRenderDebug;
    instance.allowStaticCachedForward = object.desc.allowStaticCachedForward;
    instance.skinned = surface.IsSkinned();
    instance.clusteredGeometryPath = object.desc.clusteredGeometryPath;
    const SceneRenderMaterialOverride* materialOverride = FindMaterialOverride(object.desc, surface.materialIndex);
    if (materialOverride != nullptr) {
        instance.materialOverride = materialOverride->material;
        instance.materialOverrideRevision = materialOverride->materialRevision;
    }
    instance.editorSelectionOutline = object.desc.editorSelectionMaterialIndex == surface.materialIndex;
    instance.materialFxProfileId = object.desc.materialFxProfileId;
    instance.postGroupMask = object.desc.postGroupMask;
    instance.materialFxValuesInitialized = object.desc.materialFxValuesInitialized;
    CopyMaterialFxValues(object.desc, instance);
    instance.foliageWind = object.desc.foliageWind;
    instance.foliageWindParams = object.desc.foliageWindParams;
    instance.waterSurface = object.desc.waterSurface;
    instance.waterSurfaceData = object.desc.waterSurfaceData;
    instance.terrainSurface = object.desc.terrainSurface;
    instance.terrainSurfaceCoreResident = false;
    instance.terrainSurfaceData = object.desc.terrainSurfaceData;
    instance.terrainSurfaceFields = object.desc.terrainSurfaceFields;
    instance.terrainSurfaceProjectionRevision = object.desc.terrainSurfaceProjectionRevision;

    instance.hasDrawWorldMatrix =
        ResolveRenderSurfaceDrawWorldMatrix(object.desc.worldTransform, surface, nodeGlobals, instance.drawWorldMatrix);
    const bool hasClusterWorldMatrix =
        ResolveClusterSurfaceWorldMatrix(object.desc.worldTransform, surface, nodeGlobals, bindNodeGlobals,
                                         object.desc.hasRuntimeAnimation, instance.clusterWorldMatrix);
    instance.worldBounds = ResolveRenderSurfaceWorldBounds(object.desc.worldBounds, surface, instance.drawWorldMatrix,
                                                           instance.hasDrawWorldMatrix);
    if (BOUNDS::IsUsable(instance.worldBounds)) {
        const MATH::Vec3 requestedPadding =
            MATH::IsFinite(object.desc.worldBoundsPadding) ? object.desc.worldBoundsPadding : MATH::Vec3{};
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

    instance.valid = IsSurfaceSourceUsable(object) && HasValidRenderSurfacePrimitive(object.desc.model, surface) &&
                     instance.hasDrawWorldMatrix && hasClusterWorldMatrix && BOUNDS::IsUsable(instance.worldBounds);
    return instance;
}
} // namespace

void SceneRenderCache::Clear() {
    objects_.clear();
    surfaceInstances_.clear();
    objectSurfaceRanges_.clear();
    dirtySurfaceIndices_.clear();
    indexById_.clear();
    pendingRemovalCount_ = 0;
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
    pendingTerrainPatches_.clear();
    surfaceFeatureFloats_.clear();
    surfaceFloatPoolHistory_.clear();
    previousPackedSurfaceScratch_.clear();
    sourceTerrainGridResolutionScratch_.clear();
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
        object.lifetimeGeneration = AllocateObjectLifetimeGeneration();
        object.version = 1;

        indexById_[desc.id.value] = objects_.size();
        objects_.push_back(std::move(object));
        MarkSurfaceTopologyDirty();
        TraceSceneChange("insert", desc, currentSyncFrame_);
        ++frameStats_.insertedCount;
        return;
    }

    SceneRenderObject& object = objects_[found->second];
    const DirtyFlags dirty = BuildDirtyFlags(object.desc, desc);
    object.lastTouchedFrame = currentSyncFrame_;
    object.valid = desc.model != nullptr;

    if (dirty.any) {
        const bool terrainContentOnly = IsTerrainContentOnlyUpdate(object.desc, desc, dirty);
        object.desc = desc;
        object.dirty = true;
        object.transformDirty = dirty.transform;
        object.materialDirty = dirty.material;
        object.modelDirty = dirty.model;
        ++object.version;
        if (dirty.model) {
            MarkSurfaceTopologyDirty();
            TraceSceneChange("model", desc, currentSyncFrame_);
        } else {
            UpdateSurfaceInstancesForObject(found->second);
            if (dirty.routing) {
                MarkSurfaceRoutingDirty();
                TraceSceneChange("routing", desc, currentSyncFrame_);
            } else {
                MarkSurfaceDataDirty();
                if (terrainContentOnly) {
                    pendingTerrainPatches_.push_back({desc.id, desc.terrainSurfaceDirtyRect});
                } else {
                    surfaceFloatPoolDirty_ = surfaceFloatPoolDirty_ || dirty.surfaceData;
                }
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
    for (SceneRenderObject& object : objects_) {
        if (!object.pendingRemoval && object.lastTouchedFrame != currentSyncFrame_) {
            TraceSceneChange("expire", object.desc, currentSyncFrame_);
            object.pendingRemoval = true;
            ++pendingRemovalCount_;
            MarkSurfaceTopologyDirty();
            ++frameStats_.removedCount;
        }
    }
    CompactRemoved();
    RefreshStats();
}

void SceneRenderCache::BeginPatchSync(uint64_t frameIndex) {
    currentSyncFrame_ = frameIndex;
    frameStats_ = {};
    dirtySurfaceIndices_.clear();
}

void SceneRenderCache::EndPatchSync() {
    CompactRemoved();
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
    // 一つずつ詰める（配列の中ほどを消して索引を作り直す）と、一度に多く消すフレームが O(N²) になる（部屋の部品が
    // 数百まとめて消えると数十 ms）。印を付けて索引から外し、同期の終わりか描画の前にまとめて詰める。
    TraceSceneChange("remove", objects_[found->second].desc, currentSyncFrame_);
    objects_[found->second].pendingRemoval = true;
    indexById_.erase(found);
    ++pendingRemovalCount_;
    ++frameStats_.removedCount;
    MarkSurfaceTopologyDirty();
}

bool SceneRenderCache::Touch(SceneRenderObjectId id) {
    const auto found = indexById_.find(id.value);
    if (!id.IsValid() || found == indexById_.end()) {
        return false;
    }
    SceneRenderObject& object = objects_[found->second];
    object.lastTouchedFrame = currentSyncFrame_;
    object.dirty = false;
    object.transformDirty = false;
    object.materialDirty = false;
    object.modelDirty = false;
    return true;
}

void SceneRenderCache::CompactRemoved() {
    if (pendingRemovalCount_ == 0u) {
        return;
    }
    std::erase_if(objects_, [](const SceneRenderObject& object) { return object.pendingRemoval; });
    pendingRemovalCount_ = 0u;
    RebuildIndex();
}

void SceneRenderCache::PreRenderSync() {
    CompactRemoved();
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

    if (repackSurfaceFloatPool) {
        RepackSurfaceFloatPool(dataVersionBeforePacking);
    } else if (!pendingTerrainPatches_.empty() && !TryPatchTerrainSurfaceFloats()) {
        RepackSurfaceFloatPool(dataVersionBeforePacking);
    }
    pendingTerrainPatches_.clear();
    RefreshStats();
}

bool SceneRenderCache::TryPatchTerrainSurfaceFloats() {
    std::vector<SceneSurfaceFloatRange> ranges{};
    for (const PendingTerrainPatch& patch : pendingTerrainPatches_) {
        const auto found = indexById_.find(patch.objectId.value);
        if (found == indexById_.end() || found->second >= objectSurfaceRanges_.size()) {
            return false;
        }
        const SceneRenderObject& object = objects_[found->second];
        const ObjectSurfaceRange range = objectSurfaceRanges_[found->second];
        if (range.begin > surfaceInstances_.size() || range.count > surfaceInstances_.size() - range.begin) {
            return false;
        }
        for (size_t index = range.begin; index < range.begin + range.count; ++index) {
            const SceneSurfaceInstance& instance = surfaceInstances_[index];
            if (!instance.terrainSurface) {
                continue;
            }
            // 縮小パックされた地形は行の対応が崩れるため全体を作り直す。
            if (!instance.terrainSurfaceCoreResident || instance.terrainSurfaceFields == nullptr ||
                instance.terrainSurfaceData.gridResolution != object.desc.terrainSurfaceData.gridResolution) {
                return false;
            }
            SceneSurfaceTerrainFloatAllocation allocation{};
            allocation.resident = true;
            allocation.gridResolution = instance.terrainSurfaceData.gridResolution;
            allocation.layerCount = instance.terrainSurfaceData.layerCount;
            allocation.offsets = instance.terrainSurfaceOffsets;
            if (!PatchTerrainSurfaceRows(surfaceFeatureFloats_, allocation, *instance.terrainSurfaceFields,
                                         patch.rect.minZ, (std::min)(patch.rect.maxZ, allocation.gridResolution),
                                         ranges)) {
                return false;
            }
        }
    }
    PublishSurfaceFloatPoolVersion(std::move(ranges));
    return true;
}

void SceneRenderCache::PublishSurfaceFloatPoolVersion(std::vector<SceneSurfaceFloatRange> ranges) {
    const uint64_t baseVersion = surfaceFloatPoolVersion_;
    surfaceFloatPoolVersion_ = AllocateSurfaceFloatPoolVersion();
    surfaceFloatPoolHistory_.push_back({baseVersion, surfaceFloatPoolVersion_, std::move(ranges)});
    if (surfaceFloatPoolHistory_.size() > kSceneSurfaceFloatPoolHistoryCapacity) {
        surfaceFloatPoolHistory_.erase(surfaceFloatPoolHistory_.begin());
    }
}

void SceneRenderCache::RepackSurfaceFloatPool(uint64_t dataVersionBeforePacking) {
    previousPackedSurfaceScratch_.resize(surfaceInstances_.size());
    sourceTerrainGridResolutionScratch_.assign(surfaceInstances_.size(), 1u);
    for (uint32_t surfaceIndex = 0u; surfaceIndex < surfaceInstances_.size(); ++surfaceIndex) {
        SceneSurfaceInstance& instance = surfaceInstances_[surfaceIndex];
        previousPackedSurfaceScratch_[surfaceIndex] = CapturePackedSurfaceMetadata(instance);

        // 前フレームの縮退結果を authoring/runtime source へ逆流させない。
        if (instance.objectIndex < objects_.size()) {
            const SceneRenderObjectDesc& source = objects_[instance.objectIndex].desc;
            instance.waterSurfaceData = source.waterSurfaceData;
            instance.terrainSurfaceData = source.terrainSurfaceData;
            instance.terrainSurfaceFields = source.terrainSurfaceFields;
            instance.terrainSurfaceProjectionRevision = source.terrainSurfaceProjectionRevision;
        }
        sourceTerrainGridResolutionScratch_[surfaceIndex] = (std::max)(instance.terrainSurfaceData.gridResolution, 1u);
        instance.terrainSurfaceOffsets = {};
        instance.waterSurfaceLiquidCoverageOffset = 0u;
        instance.terrainSurfaceCoreResident = false;
    }

    // 全 surface と water が共有する唯一の float pool を、surface 順の
    // deterministic first-fit で構築する。地形 core (field + style) は atomic に
    // 入り、必要なら同じ範囲を保ったまま格子だけを縮小する。最小格子さえ
    // 収容できない surface は明示的に non-resident とし、neutral tile を公開しない。
    SceneSurfaceFloatPoolPacker floatPool(surfaceFeatureFloats_, TERRAIN::kSurfaceFeatureFloatPoolCapacity);
    for (uint32_t surfaceIndex = 0u; surfaceIndex < surfaceInstances_.size(); ++surfaceIndex) {
        SceneSurfaceInstance& instance = surfaceInstances_[surfaceIndex];
        if (!instance.terrainSurface) {
            continue;
        }
        const SceneSurfaceTerrainFloatAllocation allocation = floatPool.PackTerrainCore({
            sourceTerrainGridResolutionScratch_[surfaceIndex],
            instance.terrainSurfaceData.layerCount,
            instance.terrainSurfaceFields.get(),
            &instance.terrainSurfaceData.layerStyles,
        });
        if (!allocation.resident) {
            continue;
        }
        instance.terrainSurfaceCoreResident = true;
        instance.terrainSurfaceData.gridResolution = allocation.gridResolution;
        instance.terrainSurfaceData.layerCount = allocation.layerCount;
        instance.terrainSurfaceOffsets = allocation.offsets;
    }

    // water coverage も同じ残量から正方形 field を確保する。
    // 入らない場合の offset 0 は shader 契約上「全面 water」の安全 fallback。
    for (SceneSurfaceInstance& instance : surfaceInstances_) {
        if (!instance.waterSurface || instance.waterSurfaceData.liquidCoverage.empty()) {
            continue;
        }
        floatPool.TryPackSquareField(instance.waterSurfaceData.liquidCoverage,
                                     instance.waterSurfaceData.coverageResolution,
                                     instance.waterSurfaceLiquidCoverageOffset);
    }

    // runtime 応答は高さに焼き込み済みの optional shading field。core と water
    // coverage を優先し、残量不足時は offset 0 へ落とす。
    for (uint32_t surfaceIndex = 0u; surfaceIndex < surfaceInstances_.size(); ++surfaceIndex) {
        SceneSurfaceInstance& instance = surfaceInstances_[surfaceIndex];
        if (!instance.terrainSurfaceCoreResident || !instance.terrainSurfaceData.runtimeResponse ||
            instance.terrainSurfaceFields == nullptr) {
            continue;
        }
        floatPool.TryPackSquareFieldResampled(
            instance.terrainSurfaceFields->deformations, sourceTerrainGridResolutionScratch_[surfaceIndex],
            instance.terrainSurfaceData.gridResolution, instance.terrainSurfaceOffsets.deformation);
    }

    // 先行 surface のサイズ変化は後続 offset も動かす。影響を受けた record
    // を明示的に data-dirty にし、routing rebuild なしで全 offset を patch する。
    bool packedStateChanged = false;
    for (uint32_t surfaceIndex = 0u; surfaceIndex < surfaceInstances_.size(); ++surfaceIndex) {
        if (previousPackedSurfaceScratch_[surfaceIndex] ==
            CapturePackedSurfaceMetadata(surfaceInstances_[surfaceIndex])) {
            continue;
        }
        MarkDirtySurfaceIndex(surfaceIndex);
        packedStateChanged = true;
    }
    if (packedStateChanged && surfaceDataVersion_ == dataVersionBeforePacking) {
        ++surfaceDataVersion_;
    }
    // 全体再配置は差分履歴を持たない。GPU 側は全量を転送し直す。
    surfaceFloatPoolHistory_.clear();
    surfaceFloatPoolVersion_ = AllocateSurfaceFloatPoolVersion();
    surfaceFloatPoolDirty_ = false;
}

SceneRenderCache::PackedSurfaceMetadata SceneRenderCache::CapturePackedSurfaceMetadata(
    const SceneSurfaceInstance& instance) {
    return {instance.terrainSurfaceData.gridResolution,
            instance.terrainSurfaceData.layerCount,
            instance.terrainSurfaceOffsets,
            instance.waterSurfaceData.coverageResolution,
            instance.waterSurfaceLiquidCoverageOffset,
            instance.terrainSurfaceCoreResident};
}

void SceneRenderCache::RestorePackedSurfaceMetadata(SceneSurfaceInstance& instance,
                                                    const PackedSurfaceMetadata& metadata) {
    instance.terrainSurfaceData.gridResolution = metadata.terrainGridResolution;
    instance.terrainSurfaceData.layerCount = metadata.terrainLayerCount;
    instance.terrainSurfaceOffsets = metadata.terrainOffsets;
    instance.waterSurfaceData.coverageResolution = metadata.waterCoverageResolution;
    instance.waterSurfaceLiquidCoverageOffset = metadata.waterCoverageOffset;
    instance.terrainSurfaceCoreResident = metadata.terrainCoreResident;
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

const std::vector<SceneRenderObject>& SceneRenderCache::GetObjects() const { return objects_; }

const std::vector<SceneSurfaceInstance>& SceneRenderCache::GetSurfaceInstances() const { return surfaceInstances_; }

const std::vector<float>* SceneRenderCache::GetSurfaceFeatureFloats() const { return &surfaceFeatureFloats_; }

std::span<const SceneSurfaceFloatPoolDelta> SceneRenderCache::GetSurfaceFeatureFloatHistory() const {
    return surfaceFloatPoolHistory_;
}

const std::vector<uint32_t>& SceneRenderCache::GetDirtySurfaceIndices() const { return dirtySurfaceIndices_; }

uint64_t SceneRenderCache::GetSurfaceVersion() const { return surfaceVersion_; }

uint64_t SceneRenderCache::GetSurfaceRoutingVersion() const { return surfaceRoutingVersion_; }

uint64_t SceneRenderCache::GetSurfaceDataVersion() const { return surfaceDataVersion_; }

uint64_t SceneRenderCache::GetSurfaceFloatPoolVersion() const { return surfaceFloatPoolVersion_; }

const SceneRenderCache::Stats& SceneRenderCache::GetStats() const {
    if (statsStale_) {
        ComputeStats();
        statsStale_ = false;
    }
    return publishedStats_;
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
    objectSurfaceRanges_.assign(objects_.size(), {});

    uint64_t reserveCount = 0;
    for (const SceneRenderObject& object : objects_) {
        if (object.desc.renderModel != nullptr && object.desc.renderModel->valid) {
            reserveCount += object.desc.renderModel->surfaces.size();
        }
    }
    surfaceInstances_.reserve(
        static_cast<size_t>((std::min)(reserveCount, static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))));

    for (size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex) {
        const SceneRenderObject& object = objects_[objectIndex];
        ObjectSurfaceRange& range = objectSurfaceRanges_[objectIndex];
        range.begin = surfaceInstances_.size();
        if (!IsSurfaceSourceUsable(object)) {
            continue;
        }

        const RenderModelAsset& renderModel = *object.desc.renderModel;
        const std::vector<MATH::Mat4> nodeGlobals = BuildObjectNodeGlobals(object);
        const std::vector<MATH::Mat4>& bindNodeGlobals =
            object.desc.hasRuntimeAnimation ? renderModel.bindNodeGlobals : nodeGlobals;
        for (size_t surfaceIndex = 0; surfaceIndex < renderModel.surfaces.size(); ++surfaceIndex) {
            const RenderSurfaceRecord& surface = renderModel.surfaces[surfaceIndex];
            surfaceInstances_.push_back(BuildSurfaceInstance(object, NUMERIC::SaturateToUint32(objectIndex), surface,
                                                             NUMERIC::SaturateToUint32(surfaceIndex), nodeGlobals,
                                                             bindNodeGlobals));
        }
        range.count = surfaceInstances_.size() - range.begin;
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
    if (objectIndex >= objectSurfaceRanges_.size()) {
        MarkSurfaceTopologyDirty();
        return;
    }
    const ObjectSurfaceRange range = objectSurfaceRanges_[objectIndex];
    if (range.begin > surfaceInstances_.size() || range.count > surfaceInstances_.size() - range.begin ||
        range.count != renderModel.surfaces.size()) {
        MarkSurfaceTopologyDirty();
        return;
    }
    const std::vector<MATH::Mat4> nodeGlobals = BuildObjectNodeGlobals(object);
    const std::vector<MATH::Mat4>& bindNodeGlobals =
        object.desc.hasRuntimeAnimation ? renderModel.bindNodeGlobals : nodeGlobals;
    uint32_t updatedSurfaceCount = 0;
    for (size_t surfaceInstanceIndex = range.begin; surfaceInstanceIndex < range.begin + range.count;
         ++surfaceInstanceIndex) {
        SceneSurfaceInstance& surfaceInstance = surfaceInstances_[surfaceInstanceIndex];
        if (surfaceInstance.objectIndex != NUMERIC::SaturateToUint32(objectIndex)) {
            MarkSurfaceTopologyDirty();
            return;
        }
        if (surfaceInstance.surfaceIndex >= renderModel.surfaces.size()) {
            MarkSurfaceTopologyDirty();
            return;
        }

        const PackedSurfaceMetadata packedMetadata = CapturePackedSurfaceMetadata(surfaceInstance);
        SceneSurfaceInstance updatedSurfaceInstance = BuildSurfaceInstance(
            object, NUMERIC::SaturateToUint32(objectIndex), renderModel.surfaces[surfaceInstance.surfaceIndex],
            surfaceInstance.surfaceIndex, nodeGlobals, bindNodeGlobals);
        RestorePackedSurfaceMetadata(updatedSurfaceInstance, packedMetadata);
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
    if (std::find(dirtySurfaceIndices_.begin(), dirtySurfaceIndices_.end(), surfaceIndex) !=
        dirtySurfaceIndices_.end()) {
        return;
    }
    dirtySurfaceIndices_.push_back(surfaceIndex);
}

void SceneRenderCache::MarkAllSurfaceInstancesDirty() {
    dirtySurfaceIndices_.clear();
    dirtySurfaceIndices_.reserve(surfaceInstances_.size());
    for (uint32_t surfaceIndex = 0; surfaceIndex < surfaceInstances_.size(); ++surfaceIndex) {
        dirtySurfaceIndices_.push_back(surfaceIndex);
    }
}

void SceneRenderCache::RefreshStats() { statsStale_ = true; }

void SceneRenderCache::ComputeStats() const {
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
            ++stats.authorVisibleObjectCount;
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

    publishedStats_ = stats;
}

} // namespace HIKARI::RENDER3D::RUNTIME
