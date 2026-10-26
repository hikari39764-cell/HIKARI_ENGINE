#include "Render3D/GpuDriven/HIKARI_GpuSceneRegistry.h"
#include "Render3D/GpuDriven/HIKARI_GpuScenePoseBuilder.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Water/HIKARI_WaterSurfaceGpuData.h"

#include <algorithm>
#include <limits>
#include <tuple>

#include "Core/Hashing/HIKARI_FnvHash.h"

#include "Core/Numeric/HIKARI_IntegerConversion.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace HIKARI::RENDER3D::GPUDRIVEN {

namespace {
void ResetPassSource(GpuDrivenPassSource& pass, const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
                     const std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>* materialSources,
                     uint32_t gpuSceneBaseIndex, GpuDrivenBackendKind backend, bool clusterEligible) {
    pass.traditionalIndirect.Reset();
    pass.instances = instances;
    pass.materialSources = materialSources;
    pass.gpuSceneBaseIndex = gpuSceneBaseIndex;
    pass.gpuSceneInstanceCount = instances != nullptr ? NUMERIC::SaturateToUint32(instances->size()) : 0u;
    pass.preferredBackend = backend;
    pass.clusterEligible = clusterEligible;
    pass.dirtyRanges.clear();
}

uint64_t BuildSourceLayoutVersion(uint64_t residencyEpoch, uint64_t layoutVersion, uint64_t routingVersion) {
    uint64_t value = HASH::kFnv1a64OffsetBasis;
    value = HASH::Fnv1aCombine(value, residencyEpoch);
    value = HASH::Fnv1aCombine(value, layoutVersion);
    value = HASH::Fnv1aCombine(value, routingVersion);
    return value;
}

RUNTIME::SurfaceFeatureFloatPoolSource BuildSurfaceFeatureFloatSource(uint64_t residencyEpoch,
                                                                      const RUNTIME::SceneRenderCache& cache) {
    return {cache.GetSurfaceFeatureFloats(), residencyEpoch, cache.GetSurfaceFloatPoolVersion(),
            cache.GetSurfaceFeatureFloatHistory()};
}

uint32_t PassInstanceFlag(GpuDrivenPassKind passKind) {
    using RUNTIME::SurfaceGpuSceneInstanceFlags;
    switch (passKind) {
    case GpuDrivenPassKind::ForwardOpaque:
        return static_cast<uint32_t>(SurfaceGpuSceneInstanceFlags::PassForwardOpaque);
    case GpuDrivenPassKind::DepthPrepass: return static_cast<uint32_t>(SurfaceGpuSceneInstanceFlags::PassDepthPrepass);
    case GpuDrivenPassKind::ForwardDepthAware:
        return static_cast<uint32_t>(SurfaceGpuSceneInstanceFlags::PassForwardDepthAware);
    case GpuDrivenPassKind::ForwardTransparent:
        return static_cast<uint32_t>(SurfaceGpuSceneInstanceFlags::PassForwardTransparent);
    case GpuDrivenPassKind::Shadow: return static_cast<uint32_t>(SurfaceGpuSceneInstanceFlags::PassShadow);
    default: return 0u;
    }
}

RUNTIME::SurfaceGpuSceneBuildStats BuildPassStatsFromGlobalScene(
    const std::vector<uint32_t>& recordIndices, const std::vector<uint32_t>& globalIndexByRecord,
    const std::vector<RUNTIME::SurfaceGpuSceneInstance>& instances) {
    using ResourceFlag = RUNTIME::SurfaceGpuSceneResourceFlags;
    RUNTIME::SurfaceGpuSceneBuildStats stats{};
    const uint32_t meshMaterialFlags =
        static_cast<uint32_t>(ResourceFlag::Mesh) | static_cast<uint32_t>(ResourceFlag::Material);
    const uint32_t clusterFlag = static_cast<uint32_t>(ResourceFlag::ClusterGeometry);
    const uint32_t shaderVisibleFlag = static_cast<uint32_t>(ResourceFlag::ClusterGeometryShaderVisible);
    const uint32_t surfaceRangeFlag = static_cast<uint32_t>(ResourceFlag::ClusterGeometrySurfaceRange);
    for (const uint32_t recordIndex : recordIndices) {
        if (recordIndex >= globalIndexByRecord.size()) {
            ++stats.skippedInvalidRecordCount;
            continue;
        }
        const uint32_t globalIndex = globalIndexByRecord[recordIndex];
        if (globalIndex == RUNTIME::kInvalidRenderSurfaceIndex || globalIndex >= instances.size()) {
            ++stats.skippedInvalidRecordCount;
            continue;
        }
        const RUNTIME::SurfaceGpuSceneInstance& instance = instances[globalIndex];
        ++stats.instanceCount;
        if ((instance.resourceFlags & meshMaterialFlags) == meshMaterialFlags) {
            ++stats.resourceBackedInstanceCount;
        } else {
            ++stats.missingResourceHandleInstanceCount;
        }
        if ((instance.resourceFlags & clusterFlag) != 0u) {
            ++stats.clusterResourceInstanceCount;
            if ((instance.resourceFlags & shaderVisibleFlag) != 0u) {
                ++stats.clusterShaderVisibleInstanceCount;
            }
            if ((instance.resourceFlags & surfaceRangeFlag) != 0u) {
                ++stats.clusterSurfaceRangeInstanceCount;
            } else {
                ++stats.clusterMissingSurfaceRangeInstanceCount;
            }
        }
    }
    stats.commandCount = stats.instanceCount;
    stats.maxCommandInstanceCount = stats.instanceCount != 0u ? 1u : 0u;
    return stats;
}

float ComputeWorldBoundsRadius(const Bounds& bounds) {
    const float ex = (bounds.max.x - bounds.min.x) * 0.5f;
    const float ey = (bounds.max.y - bounds.min.y) * 0.5f;
    const float ez = (bounds.max.z - bounds.min.z) * 0.5f;
    return std::sqrt(ex * ex + ey * ey + ez * ez);
}

float ComputeWorldBoundsMainAreaProxy(const Bounds& bounds) {
    float sx = (std::max)(bounds.max.x - bounds.min.x, 0.0f);
    float sy = (std::max)(bounds.max.y - bounds.min.y, 0.0f);
    float sz = (std::max)(bounds.max.z - bounds.min.z, 0.0f);
    if (sx < sy) {
        std::swap(sx, sy);
    }
    if (sy < sz) {
        std::swap(sy, sz);
    }
    if (sx < sy) {
        std::swap(sx, sy);
    }
    return sx * sy;
}

float ComputeDepthOccluderScore(const Bounds& bounds, float radius) {
    const float mainArea = ComputeWorldBoundsMainAreaProxy(bounds);
    return std::sqrt((std::max)(mainArea, 0.0f)) + radius * 0.15f;
}

constexpr float kDepthVisibilityMinOccluderRadius = 0.35f;
constexpr size_t kDepthVisibilityMaxOccluderRecords = 8192;

struct DepthPrepassOccluderCandidate {
    uint32_t recordIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    float radius = 0.0f;
    float score = 0.0f;
};

// Depth prepass は solid な不透明 mainline をカバーして forward の
// 過描画を消す。alpha mask は prepass 側でテクスチャ sample + clip の
// PS が必要になり prepass 自体が高くつくため除外する (forward の
// alpha mask も prepass が書いた solid 深度に対して early-Z が効く)。
// 両面は DoubleSided bucket + null PS で安価なので含める。
bool IsDepthPrepassSafeMaterial(const GpuSceneSurfaceRecord& record) {
    return !record.key.alphaMasked && !record.key.transparent && !record.key.depthAware && !record.key.materialFx &&
           !record.key.waterMaterialFx;
}

// BuildDepthPrepassOccluderRecords の収集規則と dirty patch の
// membership 判定は必ずこの述語を共有する。
bool IsDepthPrepassCoverageRecord(const GpuSceneSurfaceRecord& record) {
    if (!IsGpuSceneForwardOpaqueResidentRecord(record) || !IsDepthPrepassSafeMaterial(record)) {
        return false;
    }
    return ComputeWorldBoundsRadius(record.worldBounds) >= kDepthVisibilityMinOccluderRadius;
}

bool HasShadowCasterSafeMaterial(const GpuSceneSurfaceRecord& record) {
    const bool staticMaterialFx =
        record.key.materialFx && !record.key.waterMaterialFx && !record.key.materialFxUsesCustomVertexShader;
    return !record.key.transparent && !record.key.depthAware && !record.key.waterMaterialFx &&
           (!record.key.materialFx || staticMaterialFx);
}

GpuDrivenPassKind ResolveForwardResidentPass(const GpuSceneSurfaceRecord& record) {
    if (IsGpuSceneForwardOpaqueResidentRecord(record))
        return GpuDrivenPassKind::ForwardOpaque;
    if (IsGpuSceneForwardDepthAwareResidentRecord(record))
        return GpuDrivenPassKind::ForwardDepthAware;
    if (IsGpuSceneForwardTransparentResidentRecord(record))
        return GpuDrivenPassKind::ForwardTransparent;
    return GpuDrivenPassKind::Count;
}

bool IsGpuSceneStaticShadowCasterRecord(const GpuSceneSurfaceRecord& record) {
    if (!IsGpuSceneShadowResidentRecord(record) || !HasShadowCasterSafeMaterial(record)) {
        return false;
    }

    // Shadow の可視性は light-space の GPU 剔除に任せる。ワールド寸法だけで欠落させない。
    return true;
}

void BuildDepthPrepassOccluderRecords(const std::vector<GpuSceneSurfaceRecord>& records,
                                      const std::vector<uint32_t>& opaqueRecords, std::vector<uint32_t>& outOccluders,
                                      GpuSceneRegistryStats& stats) {
    outOccluders.clear();
    stats.depthPrepassOccluderRecordCount = 0;
    stats.depthPrepassRejectedSmallRecordCount = 0;
    stats.depthPrepassRejectedUnsafeMaterialRecordCount = 0;
    stats.depthPrepassBudgetClippedRecordCount = 0;

    std::vector<DepthPrepassOccluderCandidate> candidates;
    candidates.reserve((std::min)(opaqueRecords.size(), kDepthVisibilityMaxOccluderRecords * 2u));
    float largestFallbackRadius = -1.0f;
    uint32_t largestFallbackRecord = RUNTIME::kInvalidRenderSurfaceIndex;

    for (const uint32_t recordIndex : opaqueRecords) {
        if (recordIndex >= records.size()) {
            continue;
        }

        const GpuSceneSurfaceRecord& record = records[recordIndex];
        if (!IsGpuSceneForwardOpaqueResidentRecord(record)) {
            continue;
        }
        if (!IsDepthPrepassSafeMaterial(record)) {
            ++stats.depthPrepassRejectedUnsafeMaterialRecordCount;
            continue;
        }

        const float radius = ComputeWorldBoundsRadius(record.worldBounds);
        if (radius < kDepthVisibilityMinOccluderRadius) {
            ++stats.depthPrepassRejectedSmallRecordCount;
            if (radius > largestFallbackRadius) {
                largestFallbackRadius = radius;
                largestFallbackRecord = recordIndex;
            }
            continue;
        }

        candidates.push_back({recordIndex, radius, ComputeDepthOccluderScore(record.worldBounds, radius)});
    }

    if (candidates.size() > kDepthVisibilityMaxOccluderRecords) {
        std::sort(candidates.begin(), candidates.end(),
                  [](const DepthPrepassOccluderCandidate& a, const DepthPrepassOccluderCandidate& b) {
                      if (a.score != b.score) {
                          return a.score > b.score;
                      }
                      if (a.radius != b.radius) {
                          return a.radius > b.radius;
                      }
                      return a.recordIndex < b.recordIndex;
                  });
        stats.depthPrepassBudgetClippedRecordCount =
            NUMERIC::SaturateToUint32(candidates.size() - kDepthVisibilityMaxOccluderRecords);
        candidates.resize(kDepthVisibilityMaxOccluderRecords);
        std::sort(candidates.begin(), candidates.end(),
                  [](const DepthPrepassOccluderCandidate& a, const DepthPrepassOccluderCandidate& b) {
                      return a.recordIndex < b.recordIndex;
                  });
    }

    outOccluders.reserve(candidates.size());
    for (const DepthPrepassOccluderCandidate& candidate : candidates) {
        outOccluders.push_back(candidate.recordIndex);
    }

    if (outOccluders.empty() && largestFallbackRecord != RUNTIME::kInvalidRenderSurfaceIndex) {
        outOccluders.push_back(largestFallbackRecord);
    }
    stats.depthPrepassOccluderRecordCount = NUMERIC::SaturateToUint32(outOccluders.size());
}

void AppendDirtyRange(std::vector<GpuSceneDirtyRange>& ranges, uint32_t firstInstance, uint32_t instanceCount) {
    if (instanceCount == 0u) {
        return;
    }
    if (!ranges.empty()) {
        GpuSceneDirtyRange& last = ranges.back();
        if (last.firstInstance + last.instanceCount == firstInstance) {
            last.instanceCount += instanceCount;
            return;
        }
    }

    ranges.push_back({firstInstance, instanceCount});
}

uint64_t PackResourceHandle(uint32_t index, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32) | index;
}

std::array<uint32_t, 7> CountStaticBatchRuns(const std::vector<GpuSceneSurfaceRecord>& records,
                                             const std::vector<uint32_t>& indices) {
    std::array<uint32_t, 7> runs{};
    std::array<uint64_t, 7> previous{};
    bool hasPrevious = false;
    for (const uint32_t index : indices) {
        if (index >= records.size())
            continue;
        const auto& record = records[index];
        if (!record.valid || !record.key.resourceKeyValid)
            continue;
        const auto& key = record.key;
        const auto& r = key.resources;
        const std::array<uint64_t, 7> current{
            key.psoKey,
            key.materialKey,
            key.textureSetKey,
            key.geometryKey,
            PackResourceHandle(r.mesh.index, r.mesh.generation),
            PackResourceHandle(r.material.index, r.material.generation),
            PackResourceHandle(r.clusterGeometry.index, r.clusterGeometry.generation)};
        for (size_t i = 0; i < runs.size(); ++i) {
            if (!hasPrevious || current[i] != previous[i])
                ++runs[i];
        }
        previous = current;
        hasPrevious = true;
    }
    return runs;
}

bool ComesBeforeForStaticGpuSceneBatching(const GpuSceneSurfaceRecord& lhs, const GpuSceneSurfaceRecord& rhs) {
    const auto key = [](const GpuSceneSurfaceRecord& record) {
        const auto& r = record.key.resources;
        return std::tuple(record.key.psoKey, record.key.materialKey, record.key.textureSetKey,
                          record.key.geometryBackend, record.key.geometryKey,
                          PackResourceHandle(r.mesh.index, r.mesh.generation),
                          PackResourceHandle(r.material.index, r.material.generation),
                          PackResourceHandle(r.clusterGeometry.index, r.clusterGeometry.generation), record.key.sortKey,
                          record.sourceSurfaceInstanceIndex);
    };
    return key(lhs) < key(rhs);
}

bool HasCompatibleGpuScenePatchLayout(const GpuSceneSurfaceRecord& lhs, const GpuSceneSurfaceRecord& rhs) {
    const RUNTIME::SurfaceResourceIds& lhsResources = lhs.key.resources;
    const RUNTIME::SurfaceResourceIds& rhsResources = rhs.key.resources;
    if (lhs.key.backendRoute == RUNTIME::SurfaceBackendRoute::MeshShader &&
        rhs.key.backendRoute == RUNTIME::SurfaceBackendRoute::MeshShader) {
        // MeshShader の材質はインスタンス単位で参照する。
        // テクスチャや色の更新は既存スロットへ差分反映できる。
        return lhs.valid == rhs.valid && lhs.skinned == rhs.skinned && lhs.key.psoKey == rhs.key.psoKey &&
               lhs.key.shaderKey == rhs.key.shaderKey && lhs.key.passMask == rhs.key.passMask &&
               lhs.key.geometryBackend == rhs.key.geometryBackend && lhs.key.geometryKey == rhs.key.geometryKey &&
               lhs.key.clusterGeometryKey == rhs.key.clusterGeometryKey &&
               lhs.key.objectDataCompatible == rhs.key.objectDataCompatible &&
               lhs.key.clusterMainlineEligible == rhs.key.clusterMainlineEligible &&
               lhs.key.customVertexShader == rhs.key.customVertexShader && lhs.key.materialFx == rhs.key.materialFx &&
               lhs.key.waterMaterialFx == rhs.key.waterMaterialFx &&
               lhs.key.materialFxUsesCustomVertexShader == rhs.key.materialFxUsesCustomVertexShader &&
               lhs.key.materialFxUsesCustomPixelShader == rhs.key.materialFxUsesCustomPixelShader &&
               lhs.key.depthAware == rhs.key.depthAware && lhs.key.alphaMasked == rhs.key.alphaMasked &&
               lhs.key.transparent == rhs.key.transparent && lhs.key.doubleSided == rhs.key.doubleSided &&
               lhs.key.materialProgram == rhs.key.materialProgram && lhsResources.mesh == rhsResources.mesh &&
               lhsResources.clusterGeometry == rhsResources.clusterGeometry;
    }
    return lhs.key.sortKey == rhs.key.sortKey && lhs.key.psoKey == rhs.key.psoKey &&
           lhs.key.materialKey == rhs.key.materialKey && lhs.key.materialRevision == rhs.key.materialRevision &&
           lhs.key.textureSetKey == rhs.key.textureSetKey && lhs.key.geometryBackend == rhs.key.geometryBackend &&
           lhs.key.geometryKey == rhs.key.geometryKey && lhsResources.mesh == rhsResources.mesh &&
           lhsResources.material == rhsResources.material &&
           lhsResources.clusterGeometry == rhsResources.clusterGeometry;
}

GpuSceneStaticBatchStats SortStaticResidentRecordsForBatching(const std::vector<GpuSceneSurfaceRecord>& records,
                                                              std::vector<uint32_t>& recordIndices, bool allowReorder) {
    GpuSceneStaticBatchStats stats{};
    stats.recordCount = NUMERIC::SaturateToUint32(recordIndices.size());
    for (uint32_t recordIndex : recordIndices) {
        if (recordIndex >= records.size()) {
            continue;
        }
        if (records[recordIndex].key.resources.HasPoolHandles()) {
            ++stats.resourceBackedRecordCount;
        } else {
            ++stats.missingResourceHandleRecordCount;
        }
    }

    const auto rawRuns = CountStaticBatchRuns(records, recordIndices);
    const std::vector<uint32_t> originalOrder = recordIndices;
    if (allowReorder) {
        std::stable_sort(recordIndices.begin(), recordIndices.end(), [&](uint32_t lhsIndex, uint32_t rhsIndex) {
            if (lhsIndex >= records.size() || rhsIndex >= records.size()) {
                return lhsIndex < rhsIndex;
            }
            const GpuSceneSurfaceRecord& lhs = records[lhsIndex];
            const GpuSceneSurfaceRecord& rhs = records[rhsIndex];
            if (ComesBeforeForStaticGpuSceneBatching(lhs, rhs)) {
                return true;
            }
            if (ComesBeforeForStaticGpuSceneBatching(rhs, lhs)) {
                return false;
            }
            return lhsIndex < rhsIndex;
        });
        stats.sortApplied = true;
    }

    const size_t compareCount = (std::min)(originalOrder.size(), recordIndices.size());
    for (size_t i = 0; i < compareCount; ++i) {
        if (originalOrder[i] != recordIndices[i]) {
            ++stats.reorderedRecordCount;
        }
    }

    const auto sortedRuns = allowReorder ? CountStaticBatchRuns(records, recordIndices) : rawRuns;
    using Stats = GpuSceneStaticBatchStats;
    static constexpr std::array<uint32_t Stats::*, 7> rawFields{&Stats::rawPsoRunCount,
                                                                &Stats::rawMaterialRunCount,
                                                                &Stats::rawTextureSetRunCount,
                                                                &Stats::rawGeometryRunCount,
                                                                &Stats::rawMeshResourceRunCount,
                                                                &Stats::rawMaterialResourceRunCount,
                                                                &Stats::rawClusterResourceRunCount};
    static constexpr std::array<uint32_t Stats::*, 7> sortedFields{&Stats::sortedPsoRunCount,
                                                                   &Stats::sortedMaterialRunCount,
                                                                   &Stats::sortedTextureSetRunCount,
                                                                   &Stats::sortedGeometryRunCount,
                                                                   &Stats::sortedMeshResourceRunCount,
                                                                   &Stats::sortedMaterialResourceRunCount,
                                                                   &Stats::sortedClusterResourceRunCount};
    for (size_t i = 0; i < rawRuns.size(); ++i) {
        stats.*rawFields[i] = rawRuns[i];
        stats.*sortedFields[i] = sortedRuns[i];
    }
    return stats;
}

bool ShouldPublishStaticTraditionalStreams() {
    return HIKARI::RENDER3D::GetRenderQualitySettings().geometryPipeline ==
           HIKARI::RENDER3D::GeometryPipelineMode::TraditionalVsPs;
}

bool IsGpuSceneStaticTraditionalRecordCommon(const GpuSceneSurfaceRecord& record) {
    return record.valid && record.key.resourceKeyValid && !record.skinned &&
           record.key.backendRoute == RUNTIME::SurfaceBackendRoute::StaticVsPs &&
           (!record.key.materialFx || record.key.waterMaterialFx) && !record.hasSpecialRenderDebug &&
           HasValidGpuSceneSubmitPrimitiveTarget(record);
}

bool IsGpuSceneForwardStaticTraditionalRecord(const GpuSceneSurfaceRecord& record) {
    if (!record.forwardCandidate || !IsGpuSceneStaticTraditionalRecordCommon(record)) {
        return false;
    }

    return record.key.backendRoute == RUNTIME::SurfaceBackendRoute::StaticVsPs;
}

bool IsGpuSceneShadowStaticTraditionalRecord(const GpuSceneSurfaceRecord& record) {
    if (!record.shadowCandidate || !IsGpuSceneStaticTraditionalRecordCommon(record) ||
        !HasShadowCasterSafeMaterial(record)) {
        return false;
    }
    if (record.key.backendRoute != RUNTIME::SurfaceBackendRoute::StaticVsPs) {
        return false;
    }

    // Shadow の可視性は light-space の GPU 剔除に任せる。ワールド寸法だけで欠落させない。
    return true;
}
} // namespace

void GpuSceneRegistry::Clear() {
    ++residencyEpoch_;
    if (residencyEpoch_ == 0u) {
        ++residencyEpoch_;
    }
    surfaceRecords_.clear();
    waterSurfaceGpuData_.clear();
    waterSurfaceGpuData_.push_back(WATER::BuildWaterSurfaceGpuDataSentinel());
    meshShaderJointPalettes_.reset();
    terrainSurfaceGpuData_.clear();
    terrainSurfaceGpuData_.push_back(TERRAIN::BuildTerrainSurfaceGpuDataSentinel(1u));
    surfaceFeatureFloats_ = {};
    forwardOpaqueResidentRecordIndices_.clear();
    depthPrepassOccluderRecordIndices_.clear();
    forwardDepthAwareResidentRecordIndices_.clear();
    forwardTransparentResidentRecordIndices_.clear();
    shadowResidentRecordIndices_.clear();
    forwardOpaqueStaticTraditionalRecordIndices_.clear();
    forwardDepthAwareStaticTraditionalRecordIndices_.clear();
    forwardTransparentStaticTraditionalRecordIndices_.clear();
    shadowStaticTraditionalRecordIndices_.clear();
    globalGpuSceneIndexByRecord_.clear();
    globalGpuSceneInstances_.clear();
    globalMaterialSources_.clear();
    forwardOpaqueTraditionalStream_.Clear();
    forwardDepthAwareTraditionalStream_.Clear();
    forwardTransparentTraditionalStream_.Clear();
    shadowTraditionalStream_.Clear();
    sceneSource_.Reset();
    stats_ = {};
    layoutVersion_ = 0;
    routingVersion_ = 0;
    dataVersion_ = 0;
    sourceSurfaceCount_ = 0;
    publishStaticTraditionalStreams_ = false;
}

void GpuSceneRegistry::SyncForwardFromSceneCache(const GpuSceneRegistrySyncInput& input) {
    stats_.syncMode = GpuSceneSyncMode::None;
    stats_.frameRecordBuildCount = 0;
    stats_.frameRecordReuseCount = 0;
    stats_.patchRejection = {};
    if (input.sceneCache == nullptr) {
        Clear();
        return;
    }

    const uint64_t layoutVersion = input.sceneCache->GetSurfaceVersion();
    const uint64_t routingVersion = input.sceneCache->GetSurfaceRoutingVersion();
    const uint64_t dataVersion = input.sceneCache->GetSurfaceDataVersion();
    const RUNTIME::SurfaceFeatureFloatPoolSource surfaceFeatureFloats =
        BuildSurfaceFeatureFloatSource(residencyEpoch_, *input.sceneCache);
    const uint32_t surfaceCount = NUMERIC::SaturateToUint32(input.sceneCache->GetSurfaceInstances().size());

    const bool layoutChanged =
        layoutVersion_ != layoutVersion || routingVersion_ != routingVersion || sourceSurfaceCount_ != surfaceCount;
    const bool publishStaticTraditionalStreams = ShouldPublishStaticTraditionalStreams();
    const bool traditionalStreamPolicyChanged = publishStaticTraditionalStreams_ != publishStaticTraditionalStreams;
    if (layoutChanged || traditionalStreamPolicyChanged || !sceneSource_.HasAnyGpuSceneRanges()) {
        const GpuSceneSyncMode reason = layoutVersion_ != layoutVersion || sourceSurfaceCount_ != surfaceCount
                                            ? GpuSceneSyncMode::RebuildTopology
                                        : routingVersion_ != routingVersion ? GpuSceneSyncMode::RebuildRouting
                                        : traditionalStreamPolicyChanged    ? GpuSceneSyncMode::RebuildPolicy
                                                                            : GpuSceneSyncMode::RebuildEmpty;
        // 診断（環境変数 HIKARI_GPUSCENE_TRACE）：作り直しの理由と、面の数・版の変わり方を記録する。
        static const bool trace = std::getenv("HIKARI_GPUSCENE_TRACE") != nullptr;
        if (trace)
            std::fprintf(stderr, "[GpuScene] rebuild reason=%d surfaces=%u->%u layout=%llu->%llu routing=%llu->%llu\n",
                         static_cast<int>(reason), sourceSurfaceCount_, surfaceCount,
                         static_cast<unsigned long long>(layoutVersion_), static_cast<unsigned long long>(layoutVersion),
                         static_cast<unsigned long long>(routingVersion_), static_cast<unsigned long long>(routingVersion));
        // 経路だけの変化（材質の差し替え・表示の切り替えなど）は面の並びが同じなので、変わっていない面の記録を使い回す
        // （全部の面の資源を解決し直すと面の数に比例して数十〜百 ms かかる）。
        RebuildForwardFromSceneCache(input, reason == GpuSceneSyncMode::RebuildRouting);
        stats_.syncMode = reason;
        return;
    }

    ClearFrameDirtyRanges();
    if (dataVersion_ == dataVersion) {
        stats_.syncMode = GpuSceneSyncMode::Reuse;
        surfaceFeatureFloats_ = surfaceFeatureFloats;
        sceneSource_.surfaceFeatureFloats = surfaceFeatureFloats;
        sceneSource_.dirtyBaseSourceVersion = sceneSource_.sourceVersion;
        return;
    }

    const uint64_t patchBaseSourceVersion = dataVersion_;
    if (!TryPatchForwardDataFromSceneCache(input)) {
        const uint32_t patchRecordBuildCount = stats_.frameRecordBuildCount;
        GpuScenePatchRejection rejection = std::move(stats_.patchRejection);
        static const bool tracePatch = std::getenv("HIKARI_GPUSCENE_TRACE") != nullptr;
        if (tracePatch)
            std::fprintf(stderr, "[GpuScene] rebuild patch-rejected reason=%s object=%llu model=%s\n", rejection.reason,
                         static_cast<unsigned long long>(rejection.objectId), rejection.modelPath.c_str());
        RebuildForwardFromSceneCache(input, true);
        stats_.frameRecordBuildCount += patchRecordBuildCount;
        stats_.patchRejection = std::move(rejection);
        stats_.syncMode = GpuSceneSyncMode::RebuildPatchRejected;
        return;
    }

    stats_.syncMode = GpuSceneSyncMode::Patch;
    dataVersion_ = dataVersion;
    surfaceFeatureFloats_ = surfaceFeatureFloats;
    sceneSource_.surfaceFeatureFloats = surfaceFeatureFloats;
    sceneSource_.layoutVersion = BuildSourceLayoutVersion(residencyEpoch_, layoutVersion_, routingVersion_);
    sceneSource_.sourceVersion = dataVersion_;
    sceneSource_.dirtyBaseSourceVersion = patchBaseSourceVersion;
}

void GpuSceneRegistry::RebuildForwardFromSceneCache(const GpuSceneRegistrySyncInput& input,
                                                    bool reuseUnchangedRecords) {
    static const auto timer = CPU_PROFILE::RegisterDynamicPass("Render.GpuSceneRebuild");
    const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
    std::vector<GpuSceneSurfaceRecord> previousRecords;
    std::vector<uint8_t> dirtyRecordMask;
    if (reuseUnchangedRecords && input.sceneCache != nullptr &&
        layoutVersion_ == input.sceneCache->GetSurfaceVersion() &&
        surfaceRecords_.size() == input.sceneCache->GetSurfaceInstances().size()) {
        // 経路の再構築でも、変更のない表面の資源解決は再利用する。
        previousRecords = std::move(surfaceRecords_);
        dirtyRecordMask.assign(previousRecords.size(), 0u);
        for (const uint32_t index : input.sceneCache->GetDirtySurfaceIndices()) {
            if (index < dirtyRecordMask.size()) {
                dirtyRecordMask[index] = 1u;
            }
        }
    }
    Clear();
    if (input.sceneCache == nullptr) {
        return;
    }
    publishStaticTraditionalStreams_ = ShouldPublishStaticTraditionalStreams();

    const std::vector<RUNTIME::SceneSurfaceInstance>& surfaces = input.sceneCache->GetSurfaceInstances();
    surfaceRecords_.reserve(surfaces.size());
    for (uint32_t surfaceIndex = 0; surfaceIndex < surfaces.size(); ++surfaceIndex) {
        const RUNTIME::SceneSurfaceInstance& surface = surfaces[surfaceIndex];
        GpuSceneSurfaceRecord record{};
        if (surfaceIndex < previousRecords.size() && dirtyRecordMask[surfaceIndex] == 0u &&
            previousRecords[surfaceIndex].valid && previousRecords[surfaceIndex].objectId == surface.objectId &&
            previousRecords[surfaceIndex].objectLifetimeGeneration == surface.objectLifetimeGeneration &&
            previousRecords[surfaceIndex].objectVersion == surface.objectVersion &&
            previousRecords[surfaceIndex].materialOverrideRevision == surface.materialOverrideRevision &&
            previousRecords[surfaceIndex].sourceSurface == &surface) {
            record = std::move(previousRecords[surfaceIndex]);
            ++stats_.frameRecordReuseCount;
        } else {
            record = BuildGpuSceneSurfaceRecord(surface, surfaceIndex);
            ++stats_.frameRecordBuildCount;
        }
        if (!record.visible) {
            ++stats_.hiddenSourceRecordCount;
        }
        surfaceRecords_.push_back(std::move(record));
    }

    for (GpuSceneSurfaceRecord& record : surfaceRecords_) {
        if (!record.valid || !record.waterSurface) {
            record.waterSurfaceGpuDataIndex = 0u;
            continue;
        }
        record.waterSurfaceGpuDataIndex = NUMERIC::SaturateToUint32(waterSurfaceGpuData_.size());
        waterSurfaceGpuData_.push_back(
            WATER::BuildWaterSurfaceGpuData(record.waterSurfaceData, record.waterSurfaceLiquidCoverageOffset));
    }

    auto jointPaletteSnapshot = std::make_shared<GpuSceneJointPaletteSnapshot>();
    GpuScenePoseBuilder meshShaderPoseBuilder{};
    for (uint32_t recordIndex = 0; recordIndex < surfaceRecords_.size(); ++recordIndex) {
        GpuSceneSurfaceRecord& record = surfaceRecords_[recordIndex];
        record.jointPaletteOffsetBytes = 0u;
        record.jointPaletteMatrixCount = 0u;
        if (!record.skinned || record.key.backendRoute != RUNTIME::SurfaceBackendRoute::MeshShader) {
            continue;
        }
        const std::vector<MATH::Mat4>* palette = meshShaderPoseBuilder.ResolveJointPalette(record);
        if (palette == nullptr || palette->empty() || palette->size() > kMaxGpuSceneJointMatricesPerSurface ||
            jointPaletteSnapshot->matrices.size() > kMaxGpuSceneJointPaletteMatrixCount ||
            palette->size() > kMaxGpuSceneJointPaletteMatrixCount - jointPaletteSnapshot->matrices.size()) {
            continue;
        }
        record.jointPaletteOffsetBytes =
            static_cast<uint32_t>(jointPaletteSnapshot->matrices.size() * sizeof(MATH::Mat4));
        record.jointPaletteMatrixCount = NUMERIC::SaturateToUint32(palette->size());
        jointPaletteSnapshot->matrices.insert(jointPaletteSnapshot->matrices.end(), palette->begin(), palette->end());
    }
    meshShaderJointPalettes_ = std::move(jointPaletteSnapshot);

    terrainSurfaceGpuData_.clear();
    terrainSurfaceGpuData_.push_back(TERRAIN::BuildTerrainSurfaceGpuDataSentinel(1u));
    materialProgramSurfacesPresent_ =
        std::any_of(surfaceRecords_.begin(), surfaceRecords_.end(),
                    [](const GpuSceneSurfaceRecord& record) { return record.valid && record.key.materialProgram; });
    surfaceFeatureFloats_ = BuildSurfaceFeatureFloatSource(residencyEpoch_, *input.sceneCache);
    for (GpuSceneSurfaceRecord& record : surfaceRecords_) {
        if (!record.valid || !record.terrainSurface) {
            record.terrainSurfaceGpuDataIndex = 0u;
            continue;
        }
        record.terrainSurfaceGpuDataIndex = NUMERIC::SaturateToUint32(terrainSurfaceGpuData_.size());
        terrainSurfaceGpuData_.push_back(
            TERRAIN::BuildTerrainSurfaceGpuData(record.terrainSurfaceData, record.terrainSurfaceOffsets));
    }

    stats_.sourceRecordCount = NUMERIC::SaturateToUint32(surfaceRecords_.size());

    for (uint32_t recordIndex = 0; recordIndex < surfaceRecords_.size(); ++recordIndex) {
        const GpuSceneSurfaceRecord& record = surfaceRecords_[recordIndex];
        const bool meshShaderMaterialFxRecord = record.key.materialFx && !record.key.materialFxUsesCustomVertexShader;
        if (record.valid && record.shadowCandidate && record.key.resourceKeyValid) {
            if (IsGpuSceneStaticShadowCasterRecord(record)) {
                shadowResidentRecordIndices_.push_back(recordIndex);
                if (meshShaderMaterialFxRecord) {
                    ++stats_.shadowMaterialFxMeshShaderRecordCount;
                }
                if (record.key.skinned) {
                    ++stats_.shadowSkinnedMeshShaderRecordCount;
                }
            } else if (IsGpuSceneShadowStaticTraditionalRecord(record)) {
                shadowStaticTraditionalRecordIndices_.push_back(recordIndex);
                if (meshShaderMaterialFxRecord) {
                    ++stats_.shadowMaterialFxTraditionalRecordCount;
                }
            } else {
                ++stats_.blockedShadowRecordCount;
            }
        }
        if (!record.valid || !record.forwardCandidate || !record.key.resourceKeyValid)
            continue;
        ++stats_.forwardRoutedRecordCount;
        std::vector<uint32_t>* resident = nullptr;
        switch (ResolveForwardResidentPass(record)) {
        case GpuDrivenPassKind::ForwardOpaque:
            resident = &forwardOpaqueResidentRecordIndices_;
            ++stats_.forwardOpaqueClusterCandidateRecordCount;
            break;
        case GpuDrivenPassKind::ForwardDepthAware: resident = &forwardDepthAwareResidentRecordIndices_; break;
        case GpuDrivenPassKind::ForwardTransparent: resident = &forwardTransparentResidentRecordIndices_; break;
        default: break;
        }
        if (resident) {
            resident->push_back(recordIndex);
            if (meshShaderMaterialFxRecord)
                ++stats_.forwardMaterialFxMeshShaderRecordCount;
            if (record.key.skinned)
                ++stats_.forwardSkinnedMeshShaderRecordCount;
        } else if (IsGpuSceneForwardStaticTraditionalRecord(record)) {
            if (meshShaderMaterialFxRecord) {
                ++stats_.forwardMaterialFxTraditionalRecordCount;
            }
            if (record.key.depthAware) {
                forwardDepthAwareStaticTraditionalRecordIndices_.push_back(recordIndex);
            } else if (record.key.transparent) {
                forwardTransparentStaticTraditionalRecordIndices_.push_back(recordIndex);
            } else {
                forwardOpaqueStaticTraditionalRecordIndices_.push_back(recordIndex);
            }
        } else if (record.forwardCandidate) {
            ++stats_.unsupportedForwardRecordCount;
            if (meshShaderMaterialFxRecord) {
                ++stats_.forwardMaterialFxBlockedRecordCount;
            }
            if (record.key.depthAware) {
                ++stats_.blockedForwardDepthAwareRecordCount;
            } else if (record.key.transparent) {
                ++stats_.blockedForwardTransparentRecordCount;
            }
        }
    }
    stats_.forwardOpaqueResidentRecordCount = NUMERIC::SaturateToUint32(forwardOpaqueResidentRecordIndices_.size());
    stats_.forwardStaticTraditionalRecordCount = NUMERIC::SaturateToUint32(
        forwardOpaqueStaticTraditionalRecordIndices_.size() + forwardDepthAwareStaticTraditionalRecordIndices_.size() +
        forwardTransparentStaticTraditionalRecordIndices_.size());
    stats_.shadowStaticTraditionalRecordCount = NUMERIC::SaturateToUint32(shadowStaticTraditionalRecordIndices_.size());
    stats_.strictMainlineBlockedRecordCount = stats_.blockedForwardDepthAwareRecordCount +
                                              stats_.blockedForwardTransparentRecordCount +
                                              stats_.blockedShadowRecordCount;

    stats_.forwardOpaqueBatchStats =
        SortStaticResidentRecordsForBatching(surfaceRecords_, forwardOpaqueResidentRecordIndices_, true);
    BuildDepthPrepassOccluderRecords(surfaceRecords_, forwardOpaqueResidentRecordIndices_,
                                     depthPrepassOccluderRecordIndices_, stats_);
    stats_.forwardDepthAwareBatchStats =
        SortStaticResidentRecordsForBatching(surfaceRecords_, forwardDepthAwareResidentRecordIndices_, true);
    stats_.forwardTransparentBatchStats =
        SortStaticResidentRecordsForBatching(surfaceRecords_, forwardTransparentResidentRecordIndices_, false);
    stats_.shadowBatchStats = SortStaticResidentRecordsForBatching(surfaceRecords_, shadowResidentRecordIndices_, true);

    globalGpuSceneIndexByRecord_.assign(surfaceRecords_.size(), RUNTIME::kInvalidRenderSurfaceIndex);
    std::vector<uint32_t> globalRecordIndices{};
    globalRecordIndices.reserve(forwardOpaqueResidentRecordIndices_.size() + depthPrepassOccluderRecordIndices_.size() +
                                forwardDepthAwareResidentRecordIndices_.size() +
                                forwardTransparentResidentRecordIndices_.size() + shadowResidentRecordIndices_.size());

    const auto appendGlobalRecords = [&](const std::vector<uint32_t>& recordIndices) {
        for (const uint32_t recordIndex : recordIndices) {
            if (recordIndex >= globalGpuSceneIndexByRecord_.size()) {
                continue;
            }
            if (globalGpuSceneIndexByRecord_[recordIndex] != RUNTIME::kInvalidRenderSurfaceIndex) {
                continue;
            }
            globalGpuSceneIndexByRecord_[recordIndex] = NUMERIC::SaturateToUint32(globalRecordIndices.size());
            globalRecordIndices.push_back(recordIndex);
        }
    };

    for (const auto& binding : PrimaryPasses())
        appendGlobalRecords(*binding.records);

    (void)BuildGpuSceneInstanceList(surfaceRecords_, globalRecordIndices, globalGpuSceneInstances_,
                                    globalMaterialSources_);

    const auto markGlobalPass = [&](GpuDrivenPassKind passKind, const std::vector<uint32_t>& recordIndices) {
        const uint32_t passFlag = PassInstanceFlag(passKind);
        for (const uint32_t recordIndex : recordIndices) {
            if (recordIndex >= globalGpuSceneIndexByRecord_.size()) {
                continue;
            }
            const uint32_t globalIndex = globalGpuSceneIndexByRecord_[recordIndex];
            if (globalIndex == RUNTIME::kInvalidRenderSurfaceIndex || globalIndex >= globalGpuSceneInstances_.size() ||
                globalIndex >= globalMaterialSources_.size()) {
                continue;
            }
            globalGpuSceneInstances_[globalIndex].flags |= passFlag;
            globalMaterialSources_[globalIndex].localGpuSceneInstanceIndex = globalIndex;
        }
    };

    for (const auto& binding : PrimaryPasses()) {
        markGlobalPass(binding.kind, *binding.records);
        if (binding.stats) {
            *binding.stats =
                BuildPassStatsFromGlobalScene(*binding.records, globalGpuSceneIndexByRecord_, globalGpuSceneInstances_);
        }
    }

    const auto buildTraditionalStream = [&](const std::vector<uint32_t>& mainlineStaticRecordIndices,
                                            const std::vector<uint32_t>& sidecarStaticRecordIndices,
                                            RUNTIME::SurfaceDrawCommandPass pass, TraditionalIndirectStream& stream) {
        stream.Clear();
        for (const uint32_t recordIndex : sidecarStaticRecordIndices) {
            stream.Append(surfaceRecords_, recordIndex, pass);
        }

        if (publishStaticTraditionalStreams_) {
            // Resident と traditional sidecar は排他的な分類で構築済み。
            for (const uint32_t recordIndex : mainlineStaticRecordIndices) {
                stream.Append(surfaceRecords_, recordIndex, pass);
            }
        }
    };

    buildTraditionalStream(forwardOpaqueResidentRecordIndices_, forwardOpaqueStaticTraditionalRecordIndices_,
                           RUNTIME::SurfaceDrawCommandPass::Forward, forwardOpaqueTraditionalStream_);
    buildTraditionalStream(forwardDepthAwareResidentRecordIndices_, forwardDepthAwareStaticTraditionalRecordIndices_,
                           RUNTIME::SurfaceDrawCommandPass::DepthAware, forwardDepthAwareTraditionalStream_);
    buildTraditionalStream(forwardTransparentResidentRecordIndices_, forwardTransparentStaticTraditionalRecordIndices_,
                           RUNTIME::SurfaceDrawCommandPass::Forward, forwardTransparentTraditionalStream_);
    buildTraditionalStream(shadowResidentRecordIndices_, shadowStaticTraditionalRecordIndices_,
                           RUNTIME::SurfaceDrawCommandPass::Shadow, shadowTraditionalStream_);

    layoutVersion_ = input.sceneCache->GetSurfaceVersion();
    routingVersion_ = input.sceneCache->GetSurfaceRoutingVersion();
    dataVersion_ = input.sceneCache->GetSurfaceDataVersion();
    sourceSurfaceCount_ = NUMERIC::SaturateToUint32(surfaces.size());
    SuppressCpuForwardViews(input);
    RebuildForwardSceneSource();
}

void GpuSceneRegistry::SuppressCpuForwardViews(const GpuSceneRegistrySyncInput& input) {
    stats_.strictGpuDrivenMainline = true;
    stats_.cpuForwardViewSuppressedCount = input.sceneCache != nullptr ? 1u : 0u;
}

namespace {
// 診断用：書き換わった面の数（モデルの元のパスごと）。
struct DirtyCount { uint64_t surfaces = 0, staticShadow = 0; };
std::unordered_map<std::string, DirtyCount>& DirtyCounts() {
    static std::unordered_map<std::string, DirtyCount> counts;
    return counts;
}
} // namespace

bool GpuSceneRegistry::TryPatchForwardDataFromSceneCache(const GpuSceneRegistrySyncInput& input) {
    static const auto timer = CPU_PROFILE::RegisterDynamicPass("Render.GpuScenePatch");
    const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
    uint32_t currentSurfaceIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    const auto reject = [&](const char* reason) {
        auto& rejection = stats_.patchRejection;
        rejection.reason = reason;
        rejection.surfaceIndex = currentSurfaceIndex;
        if (input.sceneCache != nullptr && currentSurfaceIndex < input.sceneCache->GetSurfaceInstances().size()) {
            const auto& surface = input.sceneCache->GetSurfaceInstances()[currentSurfaceIndex];
            rejection.objectId = surface.objectId.value;
            rejection.nodeIndex = surface.nodeIndex;
            if (surface.model != nullptr) {
                rejection.modelPath = surface.model->sourcePath;
            }
        }
        return false;
    };
    if (ShouldPublishStaticTraditionalStreams()) {
        return reject("traditional pipeline policy");
    }

    if (input.sceneCache == nullptr) {
        return reject("missing scene cache");
    }
    const std::vector<RUNTIME::SceneSurfaceInstance>& surfaces = input.sceneCache->GetSurfaceInstances();
    if (surfaces.size() != surfaceRecords_.size()) {
        return reject("surface count changed");
    }

    // 診断（HIKARI_GPUSCENE_TRACE）：書き換わった面をモデルごとに数え、240 回（ほぼフレーム）ごとに多い順に出す。
    static const bool traceDirty = std::getenv("HIKARI_GPUSCENE_TRACE") != nullptr;
    if (traceDirty) {
        static uint64_t calls = 0;
        if (++calls % 240u == 0u) {
            std::vector<std::pair<std::string, DirtyCount>> sorted(DirtyCounts().begin(), DirtyCounts().end());
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.surfaces > b.second.surfaces; });
            std::fprintf(stderr, "[GpuScene] dirty top (240 calls):");
            for (size_t i = 0; i < sorted.size() && i < 10u; ++i)
                std::fprintf(stderr, " %s=%.1f(static %.1f)", sorted[i].first.c_str(), sorted[i].second.surfaces / 240.0,
                             sorted[i].second.staticShadow / 240.0);
            std::fprintf(stderr, "\n");
            DirtyCounts().clear();
        }
    }
    std::vector<uint32_t> dirtySurfaceIndices = input.sceneCache->GetDirtySurfaceIndices();
    std::sort(dirtySurfaceIndices.begin(), dirtySurfaceIndices.end());
    dirtySurfaceIndices.erase(std::unique(dirtySurfaceIndices.begin(), dirtySurfaceIndices.end()),
                              dirtySurfaceIndices.end());

    std::vector<uint32_t> singleRecordIndex{};
    singleRecordIndex.reserve(1);
    std::vector<RUNTIME::SurfaceGpuSceneInstance> singleInstance{};
    std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> singleMaterial{};
    GpuScenePoseBuilder meshShaderPoseBuilder{};
    std::shared_ptr<GpuSceneJointPaletteSnapshot> updatedJointPaletteSnapshot{};

    for (const uint32_t surfaceIndex : dirtySurfaceIndices) {
        currentSurfaceIndex = surfaceIndex;
        if (surfaceIndex >= surfaces.size() || surfaceIndex >= surfaceRecords_.size()) {
            return reject("invalid dirty index");
        }

        GpuSceneSurfaceRecord newRecord = BuildGpuSceneSurfaceRecord(surfaces[surfaceIndex], surfaceIndex);
        ++stats_.frameRecordBuildCount;
        const GpuSceneSurfaceRecord& oldRecord = surfaceRecords_[surfaceIndex];
        const bool oldMeshShaderSkinned =
            oldRecord.skinned && oldRecord.key.backendRoute == RUNTIME::SurfaceBackendRoute::MeshShader;
        const bool newMeshShaderSkinned =
            newRecord.skinned && newRecord.key.backendRoute == RUNTIME::SurfaceBackendRoute::MeshShader;
        if (oldMeshShaderSkinned != newMeshShaderSkinned) {
            return reject("joint palette routing changed");
        }
        if (newMeshShaderSkinned && oldRecord.jointPaletteMatrixCount != 0u) {
            const std::vector<MATH::Mat4>* palette = meshShaderPoseBuilder.ResolveJointPalette(newRecord);
            if (palette == nullptr || palette->empty() || palette->size() > kMaxGpuSceneJointMatricesPerSurface ||
                oldRecord.jointPaletteMatrixCount != palette->size() || meshShaderJointPalettes_ == nullptr) {
                return reject("joint palette layout changed");
            }
            const size_t firstMatrix = oldRecord.jointPaletteOffsetBytes / sizeof(MATH::Mat4);
            const size_t oldMatrixCount = oldRecord.jointPaletteMatrixCount;
            if (oldRecord.jointPaletteOffsetBytes % sizeof(MATH::Mat4) != 0u ||
                firstMatrix > meshShaderJointPalettes_->matrices.size() ||
                oldMatrixCount > meshShaderJointPalettes_->matrices.size() - firstMatrix) {
                return reject("joint palette storage changed");
            }
            const auto oldBegin = meshShaderJointPalettes_->matrices.begin() + static_cast<std::ptrdiff_t>(firstMatrix);
            const bool poseUnchanged = std::equal(palette->begin(), palette->end(), oldBegin,
                                                  [](const MATH::Mat4& lhs, const MATH::Mat4& rhs) {
                                                      for (int column = 0; column < 4; ++column) {
                                                          for (int row = 0; row < 4; ++row) {
                                                              if (lhs.m[column][row] != rhs.m[column][row]) {
                                                                  return false;
                                                              }
                                                          }
                                                      }
                                                      return true;
                                                  });
            if (!poseUnchanged) {
                if (updatedJointPaletteSnapshot == nullptr) {
                    updatedJointPaletteSnapshot =
                        std::make_shared<GpuSceneJointPaletteSnapshot>(*meshShaderJointPalettes_);
                }
                std::copy(palette->begin(), palette->end(),
                          updatedJointPaletteSnapshot->matrices.begin() + static_cast<std::ptrdiff_t>(firstMatrix));
            }
            newRecord.jointPaletteOffsetBytes = oldRecord.jointPaletteOffsetBytes;
            newRecord.jointPaletteMatrixCount = oldRecord.jointPaletteMatrixCount;
        } else if (newMeshShaderSkinned) {
            // パレット上限で描画できないレコードは、ルートとレイアウトが同じ間は
            // ブロック状態を維持し、姿勢やトランスフォームの更新で全件再構築しない。
            newRecord.jointPaletteOffsetBytes = 0u;
            newRecord.jointPaletteMatrixCount = 0u;
        }
        if (oldRecord.waterSurface != newRecord.waterSurface) {
            return reject("water membership changed");
        }
        if (IsGpuSceneForwardStaticTraditionalRecord(oldRecord) ||
            IsGpuSceneForwardStaticTraditionalRecord(newRecord) || IsGpuSceneShadowStaticTraditionalRecord(oldRecord) ||
            IsGpuSceneShadowStaticTraditionalRecord(newRecord)) {
            return reject("traditional surface");
        }
        if (oldRecord.terrainSurface != newRecord.terrainSurface) {
            return reject("sand membership changed");
        }
        const auto oldForwardPass = ResolveForwardResidentPass(oldRecord);
        const auto newForwardPass = ResolveForwardResidentPass(newRecord);
        const bool newDepthOccluder = IsDepthPrepassCoverageRecord(newRecord);
        const bool oldShadow = IsGpuSceneStaticShadowCasterRecord(oldRecord);
        const bool newShadow = IsGpuSceneStaticShadowCasterRecord(newRecord);
        if (traceDirty) {
            auto& count = DirtyCounts()[surfaces[surfaceIndex].model != nullptr ? surfaces[surfaceIndex].model->sourcePath : std::string("?")];
            ++count.surfaces;
            count.staticShadow += newShadow ? 1u : 0u;
        }
        if (oldForwardPass != newForwardPass) {
            return reject("forward pass changed");
        }
        // 遮蔽体かどうか（外接の大きさ）が変わっても組み直さない。patch は再構築時の選択（pass の印）をそのまま使い、
        // 選択が古くても描画は正しい（骨つきの人形や長さの変わる光線が閾値をまたぐたびに全件の再構築になっていた）。
        if (oldShadow != newShadow) {
            return reject("shadow coverage changed");
        }
        if (!HasCompatibleGpuScenePatchLayout(oldRecord, newRecord)) {
            return reject("geometry or material layout changed");
        }
        // 遮蔽体の予算が溢れていても組み直さない。選択は外接の大きさだけで決まり、patch は再構築時の選択（pass の印）を
        // そのまま使うので、動く物の選択が古くても描画は正しい（予算超えの場面では動く遮蔽体があるたびに毎フレーム全件
        // 再構築になっていた）。

        const uint32_t waterSurfaceGpuDataIndex = oldRecord.waterSurfaceGpuDataIndex;
        if (newRecord.waterSurface) {
            if (waterSurfaceGpuDataIndex == 0u || waterSurfaceGpuDataIndex >= waterSurfaceGpuData_.size()) {
                return reject("water slot missing");
            }
            newRecord.waterSurfaceGpuDataIndex = waterSurfaceGpuDataIndex;
        }

        const uint32_t terrainSurfaceGpuDataIndex = oldRecord.terrainSurfaceGpuDataIndex;
        if (newRecord.terrainSurface) {
            if (terrainSurfaceGpuDataIndex == 0u || terrainSurfaceGpuDataIndex >= terrainSurfaceGpuData_.size()) {
                return reject("sand slot missing");
            }
            newRecord.terrainSurfaceGpuDataIndex = terrainSurfaceGpuDataIndex;
        }

        surfaceRecords_[surfaceIndex] = std::move(newRecord);
        if (waterSurfaceGpuDataIndex != 0u) {
            waterSurfaceGpuData_[waterSurfaceGpuDataIndex] =
                WATER::BuildWaterSurfaceGpuData(surfaceRecords_[surfaceIndex].waterSurfaceData,
                                                surfaceRecords_[surfaceIndex].waterSurfaceLiquidCoverageOffset);
        }
        if (terrainSurfaceGpuDataIndex != 0u) {
            terrainSurfaceGpuData_[terrainSurfaceGpuDataIndex] = TERRAIN::BuildTerrainSurfaceGpuData(
                surfaceRecords_[surfaceIndex].terrainSurfaceData, surfaceRecords_[surfaceIndex].terrainSurfaceOffsets);
        }
        if (newForwardPass == GpuDrivenPassKind::Count && !newDepthOccluder && !newShadow) {
            continue;
        }

        singleRecordIndex.clear();
        singleRecordIndex.push_back(surfaceIndex);
        const RUNTIME::SurfaceGpuSceneBuildStats buildStats =
            BuildGpuSceneInstanceList(surfaceRecords_, singleRecordIndex, singleInstance, singleMaterial);
        if (buildStats.instanceCount != 1u || singleInstance.size() != 1u || singleMaterial.size() != 1u) {
            return reject("instance build failed");
        }

        if (surfaceIndex >= globalGpuSceneIndexByRecord_.size()) {
            return reject("record slot missing");
        }
        const uint32_t globalInstanceIndex = globalGpuSceneIndexByRecord_[surfaceIndex];
        if (globalInstanceIndex == RUNTIME::kInvalidRenderSurfaceIndex ||
            globalInstanceIndex >= globalGpuSceneInstances_.size() ||
            globalInstanceIndex >= globalMaterialSources_.size()) {
            return reject("resident instance missing");
        }

        RUNTIME::SurfaceGpuSceneInstance patchedInstance = singleInstance[0];
        // membership の検証後は既存の選択を使う。深度の小物体 fallback も維持する。
        for (const auto& binding : PrimaryPasses()) {
            const uint32_t flag = PassInstanceFlag(binding.kind);
            if ((globalGpuSceneInstances_[globalInstanceIndex].flags & flag) != 0) {
                patchedInstance.flags |= flag;
                AppendDirtyRange(sceneSource_.GetPass(binding.kind).dirtyRanges, globalInstanceIndex, 1);
            }
        }
        globalGpuSceneInstances_[globalInstanceIndex] = patchedInstance;

        RUNTIME::SurfaceGpuSceneMaterialSource materialSource = singleMaterial[0];
        materialSource.localGpuSceneInstanceIndex = globalInstanceIndex;
        globalMaterialSources_[globalInstanceIndex] = materialSource;
    }

    if (updatedJointPaletteSnapshot != nullptr) {
        meshShaderJointPalettes_ = std::move(updatedJointPaletteSnapshot);
        sceneSource_.meshShaderJointPalettes = meshShaderJointPalettes_;
    }
    return true;
}

std::array<GpuSceneRegistry::PrimaryPassBinding, 5> GpuSceneRegistry::PrimaryPasses() {
    return {{{GpuDrivenPassKind::ForwardOpaque, &forwardOpaqueResidentRecordIndices_, &forwardOpaqueTraditionalStream_,
              &stats_.forwardOpaqueGpuSceneStats},
             {GpuDrivenPassKind::DepthPrepass, &depthPrepassOccluderRecordIndices_, nullptr,
              &stats_.depthPrepassGpuSceneStats},
             {GpuDrivenPassKind::ForwardDepthAware, &forwardDepthAwareResidentRecordIndices_,
              &forwardDepthAwareTraditionalStream_, &stats_.forwardDepthAwareGpuSceneStats},
             {GpuDrivenPassKind::ForwardTransparent, &forwardTransparentResidentRecordIndices_,
              &forwardTransparentTraditionalStream_, &stats_.forwardTransparentGpuSceneStats},
             {GpuDrivenPassKind::Shadow, &shadowResidentRecordIndices_, &shadowTraditionalStream_, nullptr}}};
}

void GpuSceneRegistry::RebuildForwardSceneSource() {
    sceneSource_.surfaceRecords = &surfaceRecords_;
    sceneSource_.meshShaderJointPalettes = meshShaderJointPalettes_;
    sceneSource_.waterSurfaceGpuData = &waterSurfaceGpuData_;
    sceneSource_.terrainSurfaceGpuData = &terrainSurfaceGpuData_;
    sceneSource_.materialProgramSurfacesPresent = materialProgramSurfacesPresent_;
    sceneSource_.surfaceFeatureFloats = surfaceFeatureFloats_;
    const uint32_t sharedPrimaryCount = NUMERIC::SaturateToUint32(globalGpuSceneInstances_.size());
    uint32_t cursor = sharedPrimaryCount;

    for (const auto& binding : PrimaryPasses()) {
        auto& pass = sceneSource_.GetPass(binding.kind);
        const bool enabled = !binding.records->empty() && !globalGpuSceneInstances_.empty();
        ResetPassSource(pass, enabled ? &globalGpuSceneInstances_ : nullptr,
                        enabled ? &globalMaterialSources_ : nullptr, 0, GpuDrivenBackendKind::MeshShader, true);
        if (binding.traditional) {
            binding.traditional->Attach(pass, cursor);
            cursor += NUMERIC::SaturateToUint32(binding.traditional->instances.size());
        }
    }

    sceneSource_.layoutVersion = BuildSourceLayoutVersion(residencyEpoch_, layoutVersion_, routingVersion_);
    sceneSource_.sourceVersion = dataVersion_;
    sceneSource_.dirtyBaseSourceVersion = dataVersion_;
    sceneSource_.sourceInstanceCount = cursor;
    sceneSource_.sourceRecordCount = NUMERIC::SaturateToUint32(surfaceRecords_.size());
}

void GpuSceneRegistry::ClearFrameDirtyRanges() {
    for (GpuDrivenPassSource& pass : sceneSource_.passes) {
        pass.dirtyRanges.clear();
    }
}

const GpuDrivenSceneSource& GpuSceneRegistry::GetSceneSource() const { return sceneSource_; }

bool GpuSceneRegistry::HasShadowPassSource() const {
    const GpuDrivenPassSource& shadow = sceneSource_.GetPass(GpuDrivenPassKind::Shadow);
    return shadow.HasPrimaryGpuSceneInstances();
}

const GpuSceneRegistryStats& GpuSceneRegistry::GetStats() const { return stats_; }

const char* ToString(GpuSceneSyncMode mode) {
    switch (mode) {
    case GpuSceneSyncMode::None: return "none";
    case GpuSceneSyncMode::Reuse: return "reuse";
    case GpuSceneSyncMode::Patch: return "patch";
    case GpuSceneSyncMode::RebuildTopology: return "rebuild: topology";
    case GpuSceneSyncMode::RebuildRouting: return "rebuild: routing";
    case GpuSceneSyncMode::RebuildPolicy: return "rebuild: policy";
    case GpuSceneSyncMode::RebuildEmpty: return "rebuild: empty";
    case GpuSceneSyncMode::RebuildPatchRejected: return "rebuild: patch rejected";
    }
    return "unknown";
}

} // namespace HIKARI::RENDER3D::GPUDRIVEN
