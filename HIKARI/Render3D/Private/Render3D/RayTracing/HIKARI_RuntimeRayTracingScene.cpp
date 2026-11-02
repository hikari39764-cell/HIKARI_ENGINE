#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingScene.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingSceneState.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <tuple>
#include <vector>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_FencedUploadBuffers.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/RayTracing/HIKARI_DxrAccelerationStructure.h"
#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"
#include "Render3D/Cluster/HIKARI_ClusterRayTracingGeometry.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_MassGpuScene.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Lighting/HIKARI_LightmapRuntime.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingInputs.h"
#include "RenderCore/Resources/HIKARI_RenderResourceSystem.h"

namespace HIKARI::RENDER3D::RAYTRACING {
using namespace INTERNAL;
namespace {
using GroupKey = std::tuple<uint64_t, uint64_t, uint64_t, std::array<float, 16>>;
struct Group {
    Asset* asset = nullptr;
    MATH::Mat4 world{};
    std::vector<std::pair<uint32_t, uint32_t>> surfaces; // material slot, flags
    uint32_t index = 0;                                  // instance を並べた順の番号（GroupUse の位置）
};
template <class T>
bool SameBytes(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}
// [0, 1] の二値を unorm16 二つに詰める（HLSL の HikariRayHitLightmapUv が戻す）。
uint32_t PackUnorm16x2(float low, float high) {
    const auto unorm = [](float value) {
        return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 65535.0f));
    };
    return unorm(low) | (unorm(high) << 16u);
}
} // namespace

void RuntimeRayTracingScene::State::RetireDeformed(const GFX::Context& graphics, Deformed& entry) {
    TrackRetirement(graphics, entry.Bytes());
    for (auto& bottom : entry.bottomLevels)
        bottom->Reset(&graphics);
    entry.bottomLevels.clear();
}
void RuntimeRayTracingScene::State::TrackRetirement(const GFX::Context& graphics, uint64_t bytes) {
    if (bytes == 0 || !graphics.frameFence ||
        graphics.frameFence->GetCompletedValue() >= graphics.currentFrameRetireFenceValue)
        return;
    if (!retiring.empty() && retiring.back().fence.Get() == graphics.frameFence &&
        retiring.back().value == graphics.currentFrameRetireFenceValue)
        retiring.back().bytes += bytes;
    else
        retiring.push_back({graphics.frameFence, graphics.currentFrameRetireFenceValue, bytes});
}
void RuntimeRayTracingScene::State::RetireTop(const GFX::Context& graphics, bool keepHits) {
    sourceIdentity = nullptr;
    TrackRetirement(graphics,
                    (keepHits ? 0 : hitUploads.AllocationBytes()) + (topLevel ? topLevel->GetAllocationBytes() : 0));
    if (topLevel)
        topLevel->Reset(&graphics);
    topLevel.reset();
    if (!keepHits) {
        hitUploads.Reset(graphics, "RT.Scene.Hits");
        lastHits.clear();
    }
    lastInstances.clear();
}
uint64_t RuntimeRayTracingScene::State::Available(const RuntimeRayTracingBudget& budget) {
    stats.residentBytes =
        hitUploads.AllocationBytes() + (topLevel ? topLevel->GetAllocationBytes() : 0) + deformation.PoolBytes();
    for (const auto& [key, asset] : assets)
        stats.residentBytes += asset->Bytes();
    for (const auto& [key, entry] : deformed)
        stats.residentBytes += entry.Bytes();
    stats.retiringBytes = 0;
    for (const auto& entry : retiring)
        stats.retiringBytes += entry.bytes;
    const uint64_t used = stats.residentBytes + stats.retiringBytes;
    return used < budget.maximumBytes ? budget.maximumBytes - used : 0;
}
RuntimeRayTracingFrame RuntimeRayTracingScene::State::Fail(const GFX::Context& graphics,
                                                           const RuntimeRayTracingBudget& budget,
                                                           RuntimeRayTracingStatus status) {
    complete = false;
    RetireTop(graphics);
    Available(budget);
    stats.status = status;
    return {};
}
Asset* RuntimeRayTracingScene::State::ResolveAsset(const GFX::Context& graphics, const RuntimeRayTracingBudget& budget,
                                                   ClusterGeometryResourceHandle handle) {
    const auto* source = GetClusterGeometryResource(handle);
    auto* resource = GetRenderResourcePool().GetResource(handle);
    constexpr auto descriptorBegin = GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaBegin;
    constexpr auto descriptorEnd = descriptorBegin + GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaCapacity;
    if (!source || !source->ready || !resource || !source->metadataBuffer ||
        source->srv.descriptorIndex < descriptorBegin || source->srv.descriptorIndex >= descriptorEnd ||
        source->metadataSrv.descriptorIndex < descriptorBegin || source->metadataSrv.descriptorIndex >= descriptorEnd)
        return nullptr;
    const uint64_t key = (uint64_t(handle.generation) << 32) | handle.index;
    auto& slot = assets[key];
    if (slot &&
        (slot->sourceGeometry != resource || slot->metadata != source->metadataBuffer ||
         slot->sourceRevision != source->sourceRevision || slot->trianglesPerChunk != budget.trianglesPerChunk ||
         slot->rangesPerChunk != budget.rangesPerChunk || slot->chunks.size() > budget.chunksPerAsset ||
         slot->geometryDescriptor != source->srv.descriptorIndex ||
         slot->metadataDescriptor != source->metadataSrv.descriptorIndex)) {
        RetireTop(graphics);
        TrackRetirement(graphics, slot->Bytes());
        slot->Reset(graphics);
        slot.reset();
    }
    if (!slot) {
        auto asset = std::make_unique<Asset>();
        if (!CLUSTER::BuildClusterRayTracingChunks(*source, budget.trianglesPerChunk, budget.rangesPerChunk,
                                                   budget.chunksPerAsset, asset->chunks)) {
            assets.erase(key);
            return nullptr;
        }
        asset->sourceGeometry = resource;
        asset->sourceRevision = source->sourceRevision;
        asset->metadata = source->metadataBuffer;
        asset->geometryDescriptor = source->srv.descriptorIndex;
        asset->metadataDescriptor = source->metadataSrv.descriptorIndex;
        asset->trianglesPerChunk = budget.trianglesPerChunk;
        asset->rangesPerChunk = budget.rangesPerChunk;
        slot = std::move(asset);
    }
    slot->source = source; // Prepare 中だけ借用。次フレームは再解決する。
    slot->touched = serial;
    return slot.get();
}
RuntimeRayTracingFrame RuntimeRayTracingScene::State::WithCoverage(RuntimeRayTracingFrame frame) const {
    frame.excluded = stats.excluded.mask;
    frame.excludedShadowCasters = stats.excluded.shadowCasterMask;
    return frame;
}
RuntimeRayTracingFrame RuntimeRayTracingScene::State::Publish(const GFX::Context& graphics,
                                                              const RuntimeRayTracingBudget& budget) {
    hitUploads.MarkRead(graphics); // このフレームの光線も hit 記録を読む
    stats.status = RuntimeRayTracingStatus::Ready;
    stats.instances = static_cast<uint32_t>(lastInstances.size());
    stats.hitRecords = static_cast<uint32_t>(lastHits.size());
    stats.sharedAssets = static_cast<uint32_t>(assets.size());
    stats.readyChunks = 0;
    for (const auto& [key, asset] : assets)
        stats.readyChunks += static_cast<uint32_t>(asset->bottomLevels.size());
    Available(budget);
    RuntimeRayTracingFrame frame =
        WithCoverage({topLevel->GetGpuAddress(), hitUploads.Current()->GetGPUVirtualAddress(), stats.hitRecords,
                      version, graphics.currentFrameRetireFenceValue, true, false, graphics.frameFence,
                      graphics.deviceGeneration, sourceIdentity, layoutVersion, materialVersion});
    frame.deformedVertices = deformed.empty() ? 0 : deformation.PoolAddress();
    frame.changedMinimum = changes.minimum;
    frame.changedMaximum = changes.maximum;
    return frame;
}

RuntimeRayTracingScene::RuntimeRayTracingScene() : state_(std::make_unique<State>()) {}
RuntimeRayTracingScene::~RuntimeRayTracingScene() { Reset(GFX::GetCurrentContext()); }
const RuntimeRayTracingStats& RuntimeRayTracingScene::GetStats() const { return state_->stats; }

void RuntimeRayTracingScene::Reset(const GFX::Context& graphics) {
    auto& state = *state_;
    state.RetireTop(graphics);
    for (auto& [key, asset] : state.assets) {
        state.TrackRetirement(graphics, asset->Bytes());
        asset->Reset(graphics);
    }
    state.assets.clear();
    for (auto& [key, entry] : state.deformed)
        state.RetireDeformed(graphics, entry);
    state.deformed.clear();
    state.deformation.Reset(graphics);
    state.changes.Reset();
    state.mass.Reset();
    state.workFence = 0;
    state.deformationBroken = false;
    state.converter.Reset(graphics);
    state.stats = {};
    state.device = nullptr;
}

RuntimeRayTracingFrame RuntimeRayTracingScene::Prepare(
    const GFX::Context& graphics, const GPUDRIVEN::GpuDrivenSceneSource& source,
    std::span<const uint32_t> materialSlots, std::span<const uint32_t> massMaterialSlots,
    std::span<const MATERIAL::GpuMaterialData> materials, const RuntimeRayTracingBudget& budget,
    RuntimeRayTracingMaterialVersion materialVersion, const RuntimeRayTracingSurfaceBindings& surfaces) {
    static const auto timer = CPU_PROFILE::RegisterDynamicPass("Render.RayScene");
    const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
    auto& state = *state_;
    state.stats = {};
    if (!graphics.device || !graphics.cmdList || !graphics.frameFence || !graphics.deferredReleaseQueue ||
        graphics.currentFrameRetireFenceValue == 0 || !source.surfaceRecords || budget.buildsPerFrame == 0 ||
        budget.geometryUploadsPerFrame == 0 || budget.maximumBytes == 0 || budget.maximumInstances == 0 ||
        budget.maximumInstances > (1u << 24) || budget.maximumHitRecords == 0 || budget.maximumHitRecords > (1u << 24))
        return state.Fail(graphics, budget, RuntimeRayTracingStatus::InvalidInput);
    if (state.device != graphics.device || state.deviceGeneration != graphics.deviceGeneration) {
        Reset(graphics);
        state.device = graphics.device;
        state.deviceGeneration = graphics.deviceGeneration;
        state.supportsQueries = GFX::RAYTRACING::QueryDxrCapabilities(graphics.device).SupportsInlineRayQuery();
    }
    if (state.budgetBytes > budget.maximumBytes) {
        Reset(graphics);
        state.device = graphics.device;
    }
    state.budgetBytes = budget.maximumBytes;
    if (state.workFence != graphics.currentFrameRetireFenceValue) {
        state.workFence = graphics.currentFrameRetireFenceValue;
        state.frameBuilds = state.frameUploads = 0;
        if (state.sourceIdentity != source.surfaceRecords) {
            state.changes.Reset();
            state.complete = false;
        }
        state.changes.Update(*source.surfaceRecords);
    }
    std::erase_if(state.retiring, [](const auto& entry) { return entry.fence->GetCompletedValue() >= entry.value; });
    if (!state.supportsQueries)
        return state.Fail(graphics, budget, RuntimeRayTracingStatus::UnsupportedDevice);
    if (materialSlots.size() == state.materialSlotCount && materials.size() == state.materialCount &&
        source.surfaceRecords->size() == state.sourceRecordCount &&
        state.SameStructure(source, massMaterialSlots, budget, materialVersion)) {
        // 配置か材質の値が変わっただけなら、群をまとめ直さず instance の変換と材質の版だけを直す
        // （できなければ下で全体を組み直す）。
        if (state.sourceVersion != source.sourceVersion || state.materialVersion != materialVersion ||
            state.mass.Moved(source.mass)) {
            if (auto updated =
                    state.UpdateInPlace(graphics, source, materialSlots, materials, materialVersion, budget, surfaces))
                return *updated;
        } else {
            state.stats.reusedTopLevel = true;
            state.stats.sourceSurfaces = state.sourceSurfaceCount;
            state.stats.excluded = state.lastExcluded;
            // 蒙皮は配置が同じでも毎フレーム動く。頂点を変形して BLAS と TLAS をその場で refit する。
            if (!state.deformed.empty() && (!state.DeformAndRefit(graphics, source, budget, surfaces) ||
                                            (state.deformedRefreshed && !state.topLevel->RefitInPlace(graphics)))) {
                state.deformationBroken = true;
                return state.Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
            }
            return state.Publish(graphics, budget);
        }
    }
    static const auto rebuildTimer = CPU_PROFILE::RegisterDynamicPass("Render.RayScene.Rebuild");
    const CPU_PROFILE::ScopedDynamicCpuTimer rebuildScope(rebuildTimer);
    const auto& records = *source.surfaceRecords;
    // 入れられない面は種類を数えて外す（場面全体は止めない）。どの機能が使えるかは外した種類で決まる。
    using enum RuntimeRayTracingExclusion;
    RuntimeRayTracingExclusions& excluded = state.stats.excluded;
    std::vector<bool> included(records.size(), false);
    std::vector<bool> deforms(records.size(), false);
    // 次のフレームが配置だけの更新で済ませられるよう、表面と群の入れ方を残す。
    std::vector<RecordUse> recordUses(records.size());
    std::vector<Group*> groupOf(records.size(), nullptr);
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        recordUses[i].inputs = ReadRayRecordInputs(record, i, materialSlots, materials);
        recordUses[i].materialSlot = i < materialSlots.size() ? materialSlots[i] : 0xffffffffu;
        if (!record.visible)
            continue;
        ++state.stats.sourceSurfaces;
        // 骨格の蒙皮は描画と同じ骨格の行列で、地形は高さ場で頂点を変形して入れる（変形の pass が使えない時は外す）。
        // 風に揺れる植生・自定义頂点・材質特効は静止した形で入れる（拡散 GI と反射には足りる。影は揺れを追わない）。
        const auto exclusion = ClassifyRayExclusion(record);
        const bool deformSkin = exclusion == Skinned && record.jointPaletteMatrixCount != 0 && !state.deformationBroken;
        const bool deformTerrain = exclusion == Terrain && record.terrainSurfaceGpuDataIndex != 0 &&
                                   surfaces.terrainRecords != 0 && surfaces.surfaceFloats != 0 &&
                                   !state.deformationBroken;
        const bool approximate = exclusion == FoliageWind || exclusion == CustomVertex || exclusion == MaterialEffect;
        if (exclusion && !deformSkin && !deformTerrain && !approximate) {
            excluded.Add(*exclusion, record.castShadow);
            continue;
        }
        if (approximate)
            excluded.Approximate(*exclusion);
        if (!record.valid || !IsValidRayTransform(record.clusterWorldMatrix) || i >= materialSlots.size() ||
            materialSlots[i] >= materials.size()) {
            excluded.Add(Missing, record.castShadow);
            continue;
        }
        const auto* program =
            MATERIAL::GetRuntimeMaterialProgramRegistry().Find({materials[materialSlots[i]].materialProgramIndex});
        if (!program || program->capabilities.usesScreen || program->capabilities.usesDerivatives) {
            excluded.Add(ScreenMaterial, record.castShadow);
            continue;
        }
        included[i] = true;
        deforms[i] = deformSkin || deformTerrain;
    }
    state.stats.sharedAssets = static_cast<uint32_t>(state.assets.size());
    ++state.serial;
    state.mass.Begin(source.mass, massMaterialSlots);
    std::map<GroupKey, Group> groups;
    std::vector<size_t> loading; // 形状の資源がまだ読み込み中の面
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        if (!included[i])
            continue;
        const auto handle = record.key.resources.clusterGeometry;
        Asset* asset = state.ResolveAsset(graphics, budget, handle);
        if (!asset) {
            loading.push_back(i);
            continue;
        }
        const auto* range =
            FindClusterGeometrySurfaceRange(handle, record.nodeIndex, record.meshIndex, record.primitiveIndex);
        if (!range || range->surfaceIndex >= asset->source->surfaceRanges.size()) {
            excluded.Add(Missing, record.castShadow);
            continue;
        }
        const uint64_t assetKey = (uint64_t(handle.generation) << 32) | handle.index;
        const std::pair<uint32_t, uint32_t> surfaceState{
            materialSlots[i], (record.key.doubleSided ? 1u : 0u) | (record.castShadow ? 2u : 0u) |
                                  (record.key.transparent ? kRuntimeRayTracingHitTransmissive : 0u)};
        if (deforms[i]) {
            Deformed& entry = state.deformed[{record.objectId.value, record.objectLifetimeGeneration, assetKey,
                                              record.terrainSurface ? record.terrainSurfaceGpuDataIndex : 0u}];
            if (entry.touched != state.serial) {
                entry.touched = state.serial;
                entry.asset = asset;
                entry.terrain = record.terrainSurface;
                entry.records.clear();
                entry.surfaces.assign(asset->source->surfaceRanges.size(), {0xffffffffu, 0u});
            }
            entry.records.push_back(i);
            entry.surfaces[range->surfaceIndex] = surfaceState;
            recordUses[i].group = kDeformedGroup;
            continue;
        }
        std::array<float, 16> matrix{};
        std::memcpy(matrix.data(), &record.clusterWorldMatrix, sizeof(matrix));
        auto& group = groups[{record.objectId.value, record.objectLifetimeGeneration, assetKey, matrix}];
        if (!group.asset) {
            group.asset = asset;
            group.world = record.clusterWorldMatrix;
            group.surfaces.resize(asset->source->surfaceRanges.size(), {0xffffffffu, 0u});
        }
        groupOf[i] = &group;
        recordUses[i].surfaceIndex = range->surfaceIndex;
        auto& surface = group.surfaces[range->surfaceIndex];
        // 同じ物体/transform/surface の競合を黙って上書きしない。
        if (surface.first != 0xffffffffu && surface.first != materialSlots[i])
            excluded.Add(Missing, record.castShadow);
        surface = surfaceState;
    }
    // 大量配置は種類ごとに形を解決する。光線は個体の変換で形の BLAS と hit 記録を共有し、当たるのはその種類の面だけ。
    // 影の光線も遮る（GI の当たった点の直接光が石や草の陰へ漏れない）。カスケードの影の pass は大量配置を描かないので、
    // 入れられなかった時も影をカスケードへ戻す意味が無く、影を落とす面としては数えない。
    struct MassShape {
        Asset* asset = nullptr;
        uint32_t surface = 0, material = 0xffffffffu, flags = 0;
    };
    const GPUDRIVEN::MassGpuSceneSource* mass = source.HasMassInstances() ? source.mass : nullptr;
    std::vector<MassShape> massShapes(mass ? mass->shapes.size() : 0u);
    for (size_t s = 0; s < massShapes.size(); ++s) {
        const auto& shape = mass->shapes[s];
        const uint32_t slot = s < massMaterialSlots.size() ? massMaterialSlots[s] : 0xffffffffu;
        const auto* program =
            slot < materials.size()
                ? MATERIAL::GetRuntimeMaterialProgramRegistry().Find({materials[slot].materialProgramIndex})
                : nullptr;
        if (program && (program->capabilities.usesScreen || program->capabilities.usesDerivatives)) {
            excluded.Add(ScreenMaterial, false);
            continue;
        }
        Asset* asset = program ? state.ResolveAsset(graphics, budget, shape.clusterGeometry) : nullptr;
        const auto* range = asset ? FindClusterGeometrySurfaceRange(shape.clusterGeometry, shape.nodeIndex,
                                                                    shape.meshIndex, shape.primitiveIndex)
                                  : nullptr;
        if (!range || range->surfaceIndex >= asset->source->surfaceRanges.size()) {
            excluded.Add(MassInstances, false);
            continue;
        }
        massShapes[s] = {asset, range->surfaceIndex, slot, (shape.doubleSided ? 1u : 0u) | 2u};
    }
    const bool massPresent =
        std::any_of(massShapes.begin(), massShapes.end(), [](const MassShape& shape) { return shape.asset; });
    for (auto it = state.deformed.begin(); it != state.deformed.end();) {
        if (it->second.touched == state.serial) {
            ++it;
            continue;
        }
        state.RetireDeformed(graphics, it->second);
        it = state.deformed.erase(it);
    }
    for (auto it = state.assets.begin(); it != state.assets.end();) {
        if (it->second->touched == state.serial) {
            ++it;
            continue;
        }
        state.RetireTop(graphics);
        state.TrackRetirement(graphics, it->second->Bytes());
        it->second->Reset(graphics);
        it = state.assets.erase(it);
    }
    state.stats.sharedAssets = static_cast<uint32_t>(state.assets.size());
    // 完成した場面へ少数の面を足した時（新しく置いた物、後から流れてきた形状）は、まだ使えない面を Pending として
    // 外し、残りで場面を出す（光追の影や GI の履歴を場面ごと止めない）。読み込み直後や場面の入れ替えのように
    // 未完成が多い時は、空に近い場面で GI が空の光を拾わないよう揃うまで待つ（読み込み中は Missing）。
    size_t placedRecords = loading.size();
    for (size_t i = 0; i < records.size(); ++i)
        placedRecords += groupOf[i] ? 1u : 0u;
    for (const auto& [key, entry] : state.deformed)
        placedRecords += entry.records.size();
    const bool completeBefore = state.complete;
    const auto deferrable = [&](size_t pendingCount) { return completeBefore && pendingCount * 4u <= placedRecords; };
    const bool loadingPending = deferrable(loading.size());
    for (const size_t i : loading)
        excluded.Add(loadingPending ? Pending : Missing, records[i].castShadow);
    state.lastExcluded = excluded;
    if ((state.lastExcluded.mask & kUnsettledExclusions) == 0u)
        state.complete = true;
    const auto publishEmpty = [&] {
        state.RetireTop(graphics);
        state.Available(budget);
        state.stats.status = RuntimeRayTracingStatus::Ready;
        return state.WithCoverage({0, 0, 0, ++state.version, graphics.currentFrameRetireFenceValue, true, true,
                                   graphics.frameFence, graphics.deviceGeneration, source.surfaceRecords,
                                   source.layoutVersion, materialVersion});
    };
    if (groups.empty() && state.deformed.empty() && !massPresent)
        return publishEmpty();
    // 静的な BLAS は静的に置かれた形状の分だけ組む（蒙皮だけに使う形状は変形の BLAS が担う）。
    std::vector<const Asset*> staticAssets;
    for (const auto& [key, group] : groups)
        staticAssets.push_back(group.asset);
    for (const auto& shape : massShapes)
        if (shape.asset)
            staticAssets.push_back(shape.asset);
    for (auto& [key, asset] : state.assets) {
        const bool staticUse = std::find(staticAssets.begin(), staticAssets.end(), asset.get()) != staticAssets.end();
        if (!asset->geometry.indices && state.frameUploads < budget.geometryUploadsPerFrame) {
            if (!state.converter.Build(graphics, *asset->source, asset->sourceGeometry, state.Available(budget),
                                       asset->geometry))
                return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
            ++state.stats.geometryUploads;
            ++state.frameUploads;
        }
        if (!asset->geometry.indices)
            ++state.stats.pendingChunks;
        while (staticUse && asset->geometry.indices && asset->bottomLevels.size() < asset->chunks.size() &&
               state.frameBuilds < budget.buildsPerFrame) {
            const auto& chunk = asset->chunks[asset->bottomLevels.size()];
            std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> descriptions(chunk.ranges.size());
            for (size_t r = 0; r < chunk.ranges.size(); ++r)
                if (!asset->geometry.DescribeTriangles(chunk.ranges[r].firstPrimitive, chunk.ranges[r].primitiveCount,
                                                       descriptions[r]))
                    return state.Fail(graphics, budget, RuntimeRayTracingStatus::InvalidInput);
            auto bottom = std::make_unique<AccelerationStructure>();
            if (!bottom->BuildBottomLevel(graphics, descriptions, state.Available(budget)))
                return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
            asset->bottomLevels.push_back(std::move(bottom));
            ++state.stats.builtChunks;
            ++state.frameBuilds;
        }
        state.stats.readyChunks += static_cast<uint32_t>(asset->bottomLevels.size());
        if (staticUse)
            state.stats.pendingChunks += static_cast<uint32_t>(asset->chunks.size() - asset->bottomLevels.size());
    }
    if (state.stats.pendingChunks) {
        // 形状の BLAS を数フレームかけて組む間も、上と同じ決まりで組み終わった形状だけの場面を出す。
        const auto built = [](const Asset& asset) { return asset.bottomLevels.size() == asset.chunks.size(); };
        std::vector<size_t> pendingRecords;
        for (size_t i = 0; i < records.size(); ++i)
            if (groupOf[i] && !built(*groupOf[i]->asset))
                pendingRecords.push_back(i);
        for (const auto& [key, entry] : state.deformed)
            if (!entry.asset->geometry.indices)
                pendingRecords.insert(pendingRecords.end(), entry.records.begin(), entry.records.end());
        if (!deferrable(pendingRecords.size() + (loadingPending ? loading.size() : 0u)))
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::Building);
        for (const size_t i : pendingRecords) {
            state.lastExcluded.Add(Pending, records[i].castShadow);
            groupOf[i] = nullptr;
            recordUses[i].group = kNoGroup;
        }
        std::erase_if(groups, [&built](const auto& entry) { return !built(*entry.second.asset); });
        for (auto it = state.deformed.begin(); it != state.deformed.end();) {
            if (it->second.asset->geometry.indices) {
                ++it;
                continue;
            }
            state.RetireDeformed(graphics, it->second);
            it = state.deformed.erase(it);
        }
    }
    // 形の BLAS を組んでいる途中の大量配置は、組み終わるまで外す（数フレームで入る）。
    for (auto& shape : massShapes) {
        if (shape.asset && shape.asset->bottomLevels.size() != shape.asset->chunks.size()) {
            state.lastExcluded.Add(Pending, false);
            shape.asset = nullptr;
        }
    }
    state.stats.excluded = state.lastExcluded;
    // 変形する面の頂点を pool に並べ（面ごとに [位置][法線][接線]）、変形して BLAS を組む。
    uint64_t deformedBytes = 0;
    for (auto& [key, entry] : state.deformed) {
        entry.job.geometryDescriptor =
            entry.asset->geometryDescriptor - GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaBegin;
        entry.job.vertexCount = entry.asset->geometry.layout.vertexCount;
        entry.job.outputBase = static_cast<uint32_t>(deformedBytes);
        deformedBytes += (uint64_t(entry.job.vertexCount) * kRuntimeRayTracingDeformedVertexBytes + 255u) & ~255ull;
    }
    if (!state.deformed.empty() && (deformedBytes > UINT32_MAX || deformedBytes > state.Available(budget) ||
                                    !state.deformation.EnsurePool(graphics, deformedBytes) ||
                                    !state.DeformAndRefit(graphics, source, budget, surfaces))) {
        state.deformationBroken = true;
        return state.Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
    }
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
    std::vector<RuntimeRayTracingHitRecord> hits;
    std::vector<GroupUse> groupUses;
    groupUses.reserve(groups.size());
    for (auto& [key, group] : groups) {
        const auto& asset = *group.asset;
        group.index = static_cast<uint32_t>(groupUses.size());
        groupUses.push_back({std::get<0>(key),
                             std::get<1>(key),
                             std::get<2>(key),
                             group.world,
                             static_cast<uint32_t>(instances.size()),
                             0,
                             group.asset,
                             static_cast<uint32_t>(hits.size()),
                             {}});
        for (size_t c = 0; c < asset.chunks.size(); ++c) {
            const auto& chunk = asset.chunks[c];
            if (std::none_of(chunk.ranges.begin(), chunk.ranges.end(), [&](const auto& range) {
                    return group.surfaces[range.surfaceIndex].first != 0xffffffffu;
                }))
                continue;
            if (instances.size() >= budget.maximumInstances ||
                chunk.ranges.size() > budget.maximumHitRecords - hits.size())
                return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
            D3D12_RAYTRACING_INSTANCE_DESC instance{};
            WriteTransform(instance, group.world);
            instance.InstanceID = static_cast<uint32_t>(hits.size());
            instance.InstanceMask = 0xff;
            instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            instance.AccelerationStructure = asset.bottomLevels[c]->GetGpuAddress();
            instances.push_back(instance);
            for (const auto& range : chunk.ranges) {
                const auto [material, flags] = group.surfaces[range.surfaceIndex];
                RuntimeRayTracingHitRecord hit{
                    asset.geometryDescriptor, asset.metadataDescriptor, material,           flags,
                    range.firstPrimitive,     range.firstCluster,       range.clusterCount, range.primitiveCount};
                const auto& surface = asset.source->surfaceRanges[range.surfaceIndex];
                if (const auto* lightmap = LIGHTMAP::Find(std::get<0>(key), surface.nodeIndex, surface.meshIndex,
                                                          surface.primitiveIndex)) {
                    hit.lightmapDescriptor = lightmap->textureDescriptorIndex;
                    hit.deformedVertexBase = PackUnorm16x2(lightmap->uvScaleBias.x, lightmap->uvScaleBias.y);
                    hit.deformedVertexCount = PackUnorm16x2(lightmap->uvScaleBias.z, lightmap->uvScaleBias.w);
                }
                hits.push_back(hit);
            }
        }
        groupUses.back().instanceCount = static_cast<uint32_t>(instances.size()) - groupUses.back().firstInstance;
        groupUses.back().surfaces = std::move(group.surfaces);
    }
    for (size_t i = 0; i < records.size(); ++i)
        if (groupOf[i])
            recordUses[i].group = groupOf[i]->index;
    // 変形した面は world 空間の頂点なので instance の変換は単位。
    for (const auto& [key, entry] : state.deformed) {
        const Asset& asset = *entry.asset;
        for (size_t c = 0; c < asset.chunks.size(); ++c) {
            const auto& chunk = asset.chunks[c];
            if (instances.size() >= budget.maximumInstances ||
                chunk.ranges.size() > budget.maximumHitRecords - hits.size())
                return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
            D3D12_RAYTRACING_INSTANCE_DESC instance{};
            instance.Transform[0][0] = instance.Transform[1][1] = instance.Transform[2][2] = 1.0f;
            instance.InstanceID = static_cast<uint32_t>(hits.size());
            // 使われない形状の面だけの塊は光線から隠す（BLAS は refit の形を揃えるため組んである）。
            const bool used = std::any_of(chunk.ranges.begin(), chunk.ranges.end(), [&](const auto& range) {
                return entry.surfaces[range.surfaceIndex].first != 0xffffffffu;
            });
            instance.InstanceMask = used ? 0xff : 0x00;
            instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            instance.AccelerationStructure = entry.bottomLevels[c]->GetGpuAddress();
            instances.push_back(instance);
            for (const auto& range : chunk.ranges) {
                const auto [material, flags] = entry.surfaces[range.surfaceIndex];
                RuntimeRayTracingHitRecord hit{asset.geometryDescriptor,
                                               asset.metadataDescriptor,
                                               material,
                                               flags | kRuntimeRayTracingHitDeformed,
                                               range.firstPrimitive,
                                               range.firstCluster,
                                               range.clusterCount,
                                               range.primitiveCount};
                hit.deformedVertexBase = entry.job.outputBase;
                hit.deformedVertexCount = entry.job.vertexCount;
                hits.push_back(hit);
            }
        }
    }
    // 大量配置は最後に並べる。種類ごとの hit 記録を個体が共有する。予算に収まらなければ大量配置だけを外す。
    const size_t massFirstInstance = instances.size(), massFirstHit = hits.size();
    const auto dropMass = [&] {
        instances.resize(massFirstInstance);
        hits.resize(massFirstHit);
        state.mass.Drop();
        state.lastExcluded.Add(MassInstances, false);
        state.stats.excluded = state.lastExcluded;
    };
    bool massIncluded = false;
    if (massPresent) {
        std::vector<RuntimeRayTracingMass::Shape> shapes(massShapes.size());
        bool fits = true;
        for (size_t s = 0; s < massShapes.size() && fits; ++s) {
            const MassShape& shape = massShapes[s];
            if (!shape.asset)
                continue;
            const Asset& asset = *shape.asset;
            for (size_t c = 0; c < asset.chunks.size() && fits; ++c) {
                const auto& chunk = asset.chunks[c];
                if (std::none_of(chunk.ranges.begin(), chunk.ranges.end(),
                                 [&](const auto& range) { return range.surfaceIndex == shape.surface; }))
                    continue;
                fits = chunk.ranges.size() <= budget.maximumHitRecords - hits.size();
                if (!fits)
                    break;
                shapes[s].push_back({asset.bottomLevels[c]->GetGpuAddress(), static_cast<uint32_t>(hits.size())});
                for (const auto& range : chunk.ranges)
                    hits.push_back({asset.geometryDescriptor, asset.metadataDescriptor,
                                    range.surfaceIndex == shape.surface ? shape.material : 0xffffffffu, shape.flags,
                                    range.firstPrimitive, range.firstCluster, range.clusterCount,
                                    range.primitiveCount});
            }
        }
        massIncluded = fits && state.mass.Append(*mass, shapes, budget.maximumInstances, instances);
        if (!massIncluded)
            dropMass();
    }
    if (instances.empty()) // 大量配置の形がまだ組み途中で、他に入れる面が無い
        return publishEmpty();
    if (SameBytes(instances, state.lastInstances) && SameBytes(hits, state.lastHits) && state.topLevel) {
        state.stats.reusedTopLevel = true;
        // 変形した BLAS は中身が動いたので、それを指す TLAS も refit する。
        if (!state.deformed.empty() && !state.topLevel->RefitInPlace(graphics))
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
    } else {
        // 収まらなければ大量配置だけを外してもう一度組む（場面全体は止めない）。
        std::unique_ptr<AccelerationStructure> top;
        bool keepHits = false;
        D3D12_RESOURCE_DESC desc{};
        while (!top) {
            keepHits = state.hitUploads.Current() && SameBytes(hits, state.lastHits);
            desc = CD3DX12_RESOURCE_DESC::Buffer(hits.size() * sizeof(RuntimeRayTracingHitRecord));
            const uint64_t bytes = keepHits ? 0 : graphics.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
            const uint64_t available = state.Available(budget);
            auto built = std::make_unique<AccelerationStructure>();
            if (bytes != UINT64_MAX && bytes < available &&
                built->BuildTopLevel(graphics, instances, available - bytes, true,
                                     state.topLevel && state.lastInstances.size() == instances.size()
                                         ? state.topLevel.get()
                                         : nullptr)) {
                top = std::move(built);
            } else if (massIncluded) {
                massIncluded = false;
                dropMass();
                if (instances.empty())
                    return publishEmpty();
            } else {
                return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
            }
        }
        if (!keepHits && !state.hitUploads.Write(graphics, hits.data(), desc.Width, "RT.Scene.Hits")) {
            state.TrackRetirement(graphics, top->GetAllocationBytes());
            top->Reset(&graphics);
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
        }
        state.RetireTop(graphics, true); // 前の hit 記録は予備として残り、GPU が読み終えたら使い回す
        state.topLevel = std::move(top);
        state.lastHits = std::move(hits);
        state.lastInstances = std::move(instances);
        ++state.version;
    }
    state.sourceIdentity = source.surfaceRecords;
    state.sourceVersion = source.sourceVersion;
    state.lightmapVersion = LIGHTMAP::GetVersion();
    state.layoutVersion = source.layoutVersion;
    state.materialVersion = materialVersion;
    state.sourceSurfaceCount = state.stats.sourceSurfaces;
    state.sourceRecordCount = source.surfaceRecords->size();
    state.materialSlotCount = materialSlots.size();
    state.materialCount = materials.size();
    state.recordUses = std::move(recordUses);
    state.groupUses = std::move(groupUses);
    state.terrainBound = surfaces.terrainRecords != 0 && surfaces.surfaceFloats != 0;
    return state.Publish(graphics, budget);
}

bool IsRuntimeRayTracingFrameCurrent(const RuntimeRayTracingFrame& frame, const GFX::Context& graphics,
                                     RuntimeRayTracingMaterialVersion materials) noexcept {
    return graphics.device && graphics.cmdList && graphics.srvHeap && graphics.frameFence &&
           graphics.deviceGeneration && graphics.currentFrameRetireFenceValue && frame.ready && frame.sceneIdentity &&
           frame.retireFenceValue == graphics.currentFrameRetireFenceValue && frame.frameFence == graphics.frameFence &&
           frame.deviceGeneration == graphics.deviceGeneration && frame.materialVersion == materials &&
           (frame.empty || (frame.accelerationStructure && frame.hitRecords && frame.hitRecordCount));
}
} // namespace HIKARI::RENDER3D::RAYTRACING
