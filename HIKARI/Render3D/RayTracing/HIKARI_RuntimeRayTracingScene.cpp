#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingScene.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <tuple>
#include <vector>
#include <d3dx12.h>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/RayTracing/HIKARI_DxrAccelerationStructure.h"
#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"
#include "Render3D/Cluster/HIKARI_ClusterRayTracingGeometry.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteUpload.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingDeformation.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingChanges.h"
#include "Render3D/Resources/HIKARI_RenderResourceSystem.h"

namespace HIKARI::RENDER3D::RAYTRACING {
using AccelerationStructure = GFX::RAYTRACING::DxrAccelerationStructure;
namespace {
struct Asset {
    CLUSTER::ClusterRayTracingGeometry geometry;
    Microsoft::WRL::ComPtr<ID3D12Resource> metadata;
    std::vector<CLUSTER::ClusterRayTracingChunk> chunks;
    std::vector<std::unique_ptr<AccelerationStructure>> bottomLevels;
    const ClusterGeometryResourceRecord* source = nullptr;
    ID3D12Resource* sourceGeometry = nullptr;
    uint64_t sourceRevision = 0;
    uint64_t touched = 0;
    uint32_t geometryDescriptor = 0;
    uint32_t metadataDescriptor = 0;
    uint32_t trianglesPerChunk = 0;
    uint32_t rangesPerChunk = 0;
    uint64_t Bytes() const {
        uint64_t bytes = geometry.indexAllocationBytes;
        for (const auto& bottom : bottomLevels)
            bytes += bottom->GetAllocationBytes();
        return bytes;
    }
    void Reset(const GFX::Context& graphics) {
        for (auto& bottom : bottomLevels)
            bottom->Reset(&graphics);
        bottomLevels.clear();
        geometry.Reset(graphics);
        GFX::RetireD3D12ObjectForFrame(metadata, graphics, "RT.Scene.Metadata");
    }
};
using GroupKey = std::tuple<uint64_t, uint64_t, uint64_t, std::array<float, 16>>;
struct Group {
    Asset* asset = nullptr;
    MATH::Mat4 world{};
    std::vector<std::pair<uint32_t, uint32_t>> surfaces; // material slot, flags
};
// 変形する面。物体と形状ごとに BLAS を持ち、頂点は compute で world 空間へ変形して refit する。
// 骨格の蒙皮は毎フレーム、地形（高さ場。鍵の 4 番目は地形の記録の番号）は高さ場か配置が変わった時だけ。
using DeformedKey = std::tuple<uint64_t, uint64_t, uint64_t, uint32_t>;
struct Deformed {
    Asset* asset = nullptr;
    std::vector<size_t> records; // 変形の行列と骨格を毎フレーム読み直す表面の番号
    std::vector<std::pair<uint32_t, uint32_t>> surfaces;
    RuntimeRayTracingDeformJob job{};
    std::vector<std::unique_ptr<AccelerationStructure>> bottomLevels;
    const ID3D12Resource* builtIndices = nullptr; // BLAS を組んだ時の添字と pool（変われば組み直す）
    D3D12_GPU_VIRTUAL_ADDRESS builtPool = 0;
    uint64_t touched = 0;
    bool terrain = false;
    bool refresh = true; // このフレーム変形し直す（蒙皮は常に、地形は変化時）
    uint32_t builtOutputBase = 0xffffffffu;
    uint64_t builtFloatVersion = 0;
    std::array<float, 16> builtWorld{};
    uint64_t Bytes() const {
        uint64_t bytes = 0;
        for (const auto& bottom : bottomLevels)
            bytes += bottom->GetAllocationBytes();
        return bytes;
    }
};
// 頂点が GPU で変形する面などは今は入れられない。入れられる面は nullopt（半透明は透過する面として入れる）。
std::optional<RuntimeRayTracingExclusion> ClassifyExclusion(const GPUDRIVEN::GpuSceneSurfaceRecord& record) {
    using enum RuntimeRayTracingExclusion;
    if (record.waterSurface)
        return Water;
    if (record.skinned)
        return Skinned;
    if (record.foliageWind)
        return FoliageWind;
    if (record.terrainSurface)
        return Terrain;
    if (record.key.customVertexShader)
        return CustomVertex;
    if (record.key.materialFx)
        return MaterialEffect;
    return std::nullopt;
}
bool ValidTransform(const MATH::Mat4& world) {
    if (!MATH::IsFinite(world) || world.m[0][3] != 0 || world.m[1][3] != 0 || world.m[2][3] != 0 || world.m[3][3] != 1)
        return false;
    const auto matrix = std::bit_cast<DirectX::XMFLOAT4X4>(world);
    const float determinant = DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&matrix)));
    return std::isfinite(determinant) && determinant != 0;
}
template <class T>
bool SameBytes(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}
} // namespace

struct RuntimeRayTracingScene::State {
    struct RetiringBytes {
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        uint64_t value = 0;
        uint64_t bytes = 0;
    };
    ID3D12Device* device = nullptr;
    uint64_t deviceGeneration = 0;
    bool supportsQueries = false;
    uint64_t serial = 0;
    uint64_t version = 0;
    uint64_t budgetBytes = 0;
    uint64_t workFence = 0;
    uint32_t frameBuilds = 0;
    uint32_t frameUploads = 0;
    const void* sourceIdentity = nullptr;
    uint64_t sourceVersion = 0, layoutVersion = 0;
    uint32_t sourceSurfaceCount = 0;
    size_t sourceRecordCount = 0, materialSlotCount = 0, materialCount = 0;
    RuntimeRayTracingMaterialVersion materialVersion{};
    std::map<uint64_t, std::unique_ptr<Asset>> assets;
    CLUSTER::ClusterRayTracingGeometryBuilder converter;
    std::unique_ptr<AccelerationStructure> topLevel;
    Microsoft::WRL::ComPtr<ID3D12Resource> hitBuffer;
    uint64_t hitBufferBytes = 0;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> lastInstances;
    std::vector<RuntimeRayTracingHitRecord> lastHits;
    std::vector<RetiringBytes> retiring;
    RuntimeRayTracingStats stats;
    // 最後に組んだ時の入れなかった面（大量配置は毎フレーム足す）。加速構造を使い回すフレームも同じ値を出す。
    RuntimeRayTracingExclusions lastExcluded{};
    std::map<DeformedKey, Deformed> deformed;
    bool deformedRefreshed = false; // 直前の DeformAndRefit が頂点を書き直した（TLAS の refit が要る）
    RuntimeRayTracingDeformation deformation;
    RuntimeRayTracingChanges changes;
    // 変形の compute が使えない（shader の失敗など）時は蒙皮を外す面に戻す。
    bool deformationBroken = false;

    void RetireDeformed(const GFX::Context& graphics, Deformed& entry) {
        TrackRetirement(graphics, entry.Bytes());
        for (auto& bottom : entry.bottomLevels)
            bottom->Reset(&graphics);
        entry.bottomLevels.clear();
    }
    // 表面から変形の行列と骨格（地形は高さ場の番号）を読み直し、頂点を変形して BLAS を作るか refit する。
    bool DeformAndRefit(const GFX::Context& graphics, const GPUDRIVEN::GpuDrivenSceneSource& source,
                        const RuntimeRayTracingBudget& budget, const RuntimeRayTracingSurfaceBindings& surfaces) {
        const auto& records = *source.surfaceRecords;
        const D3D12_GPU_VIRTUAL_ADDRESS pool = deformation.PoolAddress();
        std::vector<RuntimeRayTracingDeformJob> jobs;
        jobs.reserve(deformed.size());
        bool skinned = false;
        for (auto& [key, entry] : deformed) {
            const auto& record = records[entry.records.front()];
            entry.job.world = record.clusterWorldMatrix;
            entry.job.kind = entry.terrain ? kRuntimeRayTracingDeformTerrain : kRuntimeRayTracingDeformSkinned;
            entry.job.paletteOffsetBytes =
                entry.terrain ? record.terrainSurfaceGpuDataIndex : record.jointPaletteOffsetBytes;
            entry.job.paletteMatrixCount = entry.terrain ? 0u : record.jointPaletteMatrixCount;
            std::array<float, 16> world{};
            std::memcpy(world.data(), &record.clusterWorldMatrix, sizeof(world));
            entry.refresh = !entry.terrain || entry.bottomLevels.empty() ||
                            entry.builtIndices != entry.asset->geometry.indices.Get() || entry.builtPool != pool ||
                            entry.builtOutputBase != entry.job.outputBase ||
                            entry.builtFloatVersion != surfaces.floatVersion || entry.builtWorld != world;
            if (!entry.refresh)
                continue;
            entry.builtOutputBase = entry.job.outputBase;
            entry.builtFloatVersion = surfaces.floatVersion;
            entry.builtWorld = world;
            skinned = skinned || !entry.terrain;
            jobs.push_back(entry.job);
        }
        deformedRefreshed = !jobs.empty();
        const D3D12_GPU_VIRTUAL_ADDRESS palettes =
            skinned ? GPUDRIVEN::UploadGpuSceneJointPaletteSnapshot(graphics, source.meshShaderJointPalettes) : 0;
        if (!deformation.Dispatch(graphics, jobs, palettes, surfaces.terrainRecords, surfaces.surfaceFloats))
            return false;
        for (auto& [key, entry] : deformed) {
            Asset& asset = *entry.asset;
            if (entry.builtIndices != asset.geometry.indices.Get() || entry.builtPool != pool) {
                RetireDeformed(graphics, entry);
                entry.builtIndices = asset.geometry.indices.Get();
                entry.builtPool = pool;
            }
            if (!entry.refresh && entry.bottomLevels.size() == asset.chunks.size())
                continue;
            for (size_t c = 0; c < asset.chunks.size(); ++c) {
                const auto& chunk = asset.chunks[c];
                std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> descriptions(chunk.ranges.size());
                for (size_t r = 0; r < chunk.ranges.size(); ++r)
                    if (!asset.geometry.DescribeTriangles(chunk.ranges[r].firstPrimitive,
                                                          chunk.ranges[r].primitiveCount, descriptions[r],
                                                          pool + entry.job.outputBase))
                        return false;
                if (c < entry.bottomLevels.size()) {
                    if (!entry.bottomLevels[c]->RefitInPlace(graphics, descriptions))
                        return false;
                    continue;
                }
                auto bottom = std::make_unique<AccelerationStructure>();
                if (!bottom->BuildBottomLevel(graphics, descriptions, Available(budget), nullptr, true))
                    return false;
                entry.bottomLevels.push_back(std::move(bottom));
                ++stats.builtChunks;
            }
        }
        stats.deformedObjects = static_cast<uint32_t>(deformed.size());
        return true;
    }

    void TrackRetirement(const GFX::Context& graphics, uint64_t bytes) {
        if (bytes == 0 || !graphics.frameFence ||
            graphics.frameFence->GetCompletedValue() >= graphics.currentFrameRetireFenceValue)
            return;
        if (!retiring.empty() && retiring.back().fence.Get() == graphics.frameFence &&
            retiring.back().value == graphics.currentFrameRetireFenceValue)
            retiring.back().bytes += bytes;
        else
            retiring.push_back({graphics.frameFence, graphics.currentFrameRetireFenceValue, bytes});
    }
    void RetireTop(const GFX::Context& graphics, bool keepHits = false) {
        sourceIdentity = nullptr;
        TrackRetirement(graphics, (keepHits ? 0 : hitBufferBytes) + (topLevel ? topLevel->GetAllocationBytes() : 0));
        if (topLevel)
            topLevel->Reset(&graphics);
        topLevel.reset();
        if (!keepHits) {
            GFX::RetireD3D12ObjectForFrame(hitBuffer, graphics, "RT.Scene.Hits");
            hitBufferBytes = 0;
            lastHits.clear();
        }
        lastInstances.clear();
    }
    uint64_t Available(const RuntimeRayTracingBudget& budget) {
        stats.residentBytes =
            hitBufferBytes + (topLevel ? topLevel->GetAllocationBytes() : 0) + deformation.PoolBytes();
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
    RuntimeRayTracingFrame Fail(const GFX::Context& graphics, const RuntimeRayTracingBudget& budget,
                                RuntimeRayTracingStatus status) {
        RetireTop(graphics);
        Available(budget);
        stats.status = status;
        return {};
    }
    Asset* ResolveAsset(const GFX::Context& graphics, const RuntimeRayTracingBudget& budget,
                        ClusterGeometryResourceHandle handle) {
        const auto* source = GetClusterGeometryResource(handle);
        auto* resource = GetRenderResourcePool().GetResource(handle);
        constexpr auto descriptorBegin = GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaBegin;
        constexpr auto descriptorEnd = descriptorBegin + GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaCapacity;
        if (!source || !source->ready || !resource || !source->metadataBuffer ||
            source->srv.descriptorIndex < descriptorBegin || source->srv.descriptorIndex >= descriptorEnd ||
            source->metadataSrv.descriptorIndex < descriptorBegin ||
            source->metadataSrv.descriptorIndex >= descriptorEnd)
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
    bool CanReuse(const GPUDRIVEN::GpuDrivenSceneSource& source, const RuntimeRayTracingBudget& budget,
                  RuntimeRayTracingMaterialVersion materials) {
        if (!topLevel || source.sourceVersion == 0 || materials.data == 0 || materials.bindings == 0 ||
            sourceIdentity != source.surfaceRecords || sourceVersion != source.sourceVersion ||
            layoutVersion != source.layoutVersion || materialVersion != materials ||
            (lastExcluded.mask & RuntimeRayTracingExclusionBit(RuntimeRayTracingExclusion::Missing)) != 0u)
            return false; // 未解決の面（読み込み中の形状など）があれば揃うまで毎フレーム組み直す。
        for (const auto& [key, asset] : assets) {
            const ClusterGeometryResourceHandle handle{uint32_t(key), uint32_t(key >> 32)};
            const auto* current = GetClusterGeometryResource(handle);
            if (!current || !current->ready || current->sourceRevision != asset->sourceRevision ||
                current->metadataBuffer != asset->metadata ||
                GetRenderResourcePool().GetResource(handle) != asset->sourceGeometry ||
                current->srv.descriptorIndex != asset->geometryDescriptor ||
                current->metadataSrv.descriptorIndex != asset->metadataDescriptor ||
                budget.trianglesPerChunk != asset->trianglesPerChunk ||
                budget.rangesPerChunk != asset->rangesPerChunk || asset->chunks.size() > budget.chunksPerAsset)
                return false;
        }
        Available(budget);
        return lastHits.size() <= budget.maximumHitRecords && lastInstances.size() <= budget.maximumInstances &&
               stats.residentBytes + stats.retiringBytes <= budget.maximumBytes;
    }
    // 加速構造に入れない大量配置（草や岩の散布）を足す。
    void ExcludeMassInstances(const GPUDRIVEN::GpuDrivenSceneSource& source) {
        stats.excluded = lastExcluded;
        if (source.HasMassInstances())
            stats.excluded.Add(RuntimeRayTracingExclusion::MassInstances, true);
    }
    RuntimeRayTracingFrame WithCoverage(RuntimeRayTracingFrame frame) const {
        frame.excluded = stats.excluded.mask;
        frame.excludedShadowCasters = stats.excluded.shadowCasterMask;
        return frame;
    }
    RuntimeRayTracingFrame Publish(const GFX::Context& graphics, const RuntimeRayTracingBudget& budget) {
        stats.status = RuntimeRayTracingStatus::Ready;
        stats.instances = static_cast<uint32_t>(lastInstances.size());
        stats.hitRecords = static_cast<uint32_t>(lastHits.size());
        stats.sharedAssets = static_cast<uint32_t>(assets.size());
        stats.readyChunks = 0;
        for (const auto& [key, asset] : assets)
            stats.readyChunks += static_cast<uint32_t>(asset->bottomLevels.size());
        Available(budget);
        RuntimeRayTracingFrame frame =
            WithCoverage({topLevel->GetGpuAddress(), hitBuffer->GetGPUVirtualAddress(), stats.hitRecords, version,
                          graphics.currentFrameRetireFenceValue, true, false, graphics.frameFence,
                          graphics.deviceGeneration, sourceIdentity, layoutVersion, materialVersion});
        frame.deformedVertices = deformed.empty() ? 0 : deformation.PoolAddress();
        frame.changedMinimum = changes.minimum;
        frame.changedMaximum = changes.maximum;
        return frame;
    }
};

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
    state.workFence = 0;
    state.deformationBroken = false;
    state.converter.Reset(graphics);
    state.stats = {};
    state.device = nullptr;
}

RuntimeRayTracingFrame RuntimeRayTracingScene::Prepare(const GFX::Context& graphics,
                                                       const GPUDRIVEN::GpuDrivenSceneSource& source,
                                                       std::span<const uint32_t> materialSlots,
                                                       std::span<const MATERIAL::GpuMaterialData> materials,
                                                       const RuntimeRayTracingBudget& budget,
                                                       RuntimeRayTracingMaterialVersion materialVersion,
                                                       const RuntimeRayTracingSurfaceBindings& surfaces) {
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
        if (state.sourceIdentity != source.surfaceRecords)
            state.changes.Reset();
        state.changes.Update(*source.surfaceRecords);
    }
    std::erase_if(state.retiring, [](const auto& entry) { return entry.fence->GetCompletedValue() >= entry.value; });
    if (!state.supportsQueries)
        return state.Fail(graphics, budget, RuntimeRayTracingStatus::UnsupportedDevice);
    if (materialSlots.size() == state.materialSlotCount && materials.size() == state.materialCount &&
        source.surfaceRecords->size() == state.sourceRecordCount && state.CanReuse(source, budget, materialVersion)) {
        state.stats.reusedTopLevel = true;
        state.stats.sourceSurfaces = state.sourceSurfaceCount;
        state.ExcludeMassInstances(source);
        // 蒙皮は配置が同じでも毎フレーム動く。頂点を変形して BLAS と TLAS をその場で refit する。
        if (!state.deformed.empty() && (!state.DeformAndRefit(graphics, source, budget, surfaces) ||
                                        (state.deformedRefreshed && !state.topLevel->RefitInPlace(graphics)))) {
            state.deformationBroken = true;
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
        }
        return state.Publish(graphics, budget);
    }
    const auto& records = *source.surfaceRecords;
    // 入れられない面は種類を数えて外す（場面全体は止めない）。どの機能が使えるかは外した種類で決まる。
    using enum RuntimeRayTracingExclusion;
    RuntimeRayTracingExclusions& excluded = state.stats.excluded;
    std::vector<bool> included(records.size(), false);
    std::vector<bool> deforms(records.size(), false);
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        if (!record.visible)
            continue;
        ++state.stats.sourceSurfaces;
        // 骨格の蒙皮は描画と同じ骨格の行列で、地形は高さ場で頂点を変形して入れる（変形の pass が使えない時は外す）。
        // 風に揺れる植生・自定义頂点・材質特効は静止した形で入れる（拡散 GI と反射には足りる。影は揺れを追わない）。
        const auto exclusion = ClassifyExclusion(record);
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
        if (!record.valid || !ValidTransform(record.clusterWorldMatrix) || i >= materialSlots.size() ||
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
    std::map<GroupKey, Group> groups;
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        if (!included[i])
            continue;
        const auto handle = record.key.resources.clusterGeometry;
        Asset* asset = state.ResolveAsset(graphics, budget, handle);
        const auto* range =
            asset ? FindClusterGeometrySurfaceRange(handle, record.nodeIndex, record.meshIndex, record.primitiveIndex)
                  : nullptr;
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
        auto& surface = group.surfaces[range->surfaceIndex];
        // 同じ物体/transform/surface の競合を黙って上書きしない。
        if (surface.first != 0xffffffffu && surface.first != materialSlots[i])
            excluded.Add(Missing, record.castShadow);
        surface = surfaceState;
    }
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
    state.lastExcluded = excluded;
    state.ExcludeMassInstances(source);
    if (groups.empty() && state.deformed.empty()) {
        state.RetireTop(graphics);
        state.Available(budget);
        state.stats.status = RuntimeRayTracingStatus::Ready;
        return state.WithCoverage({0, 0, 0, ++state.version, graphics.currentFrameRetireFenceValue, true, true,
                                   graphics.frameFence, graphics.deviceGeneration, source.surfaceRecords,
                                   source.layoutVersion, materialVersion});
    }
    // 静的な BLAS は静的に置かれた形状の分だけ組む（蒙皮だけに使う形状は変形の BLAS が担う）。
    std::vector<const Asset*> staticAssets;
    for (const auto& [key, group] : groups)
        staticAssets.push_back(group.asset);
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
    if (state.stats.pendingChunks)
        return state.Fail(graphics, budget, RuntimeRayTracingStatus::Building);
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
    for (const auto& [key, group] : groups) {
        const auto& asset = *group.asset;
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
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 4; ++col)
                    instance.Transform[row][col] = group.world.m[col][row];
            instance.InstanceID = static_cast<uint32_t>(hits.size());
            instance.InstanceMask = 0xff;
            instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            instance.AccelerationStructure = asset.bottomLevels[c]->GetGpuAddress();
            instances.push_back(instance);
            for (const auto& range : chunk.ranges) {
                const auto [material, flags] = group.surfaces[range.surfaceIndex];
                hits.push_back({asset.geometryDescriptor, asset.metadataDescriptor, material, flags,
                                range.firstPrimitive, range.firstCluster, range.clusterCount, range.primitiveCount});
            }
        }
    }
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
    if (SameBytes(instances, state.lastInstances) && SameBytes(hits, state.lastHits) && state.topLevel) {
        state.stats.reusedTopLevel = true;
        // 変形した BLAS は中身が動いたので、それを指す TLAS も refit する。
        if (!state.deformed.empty() && !state.topLevel->RefitInPlace(graphics))
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
    } else {
        const bool keepHits = state.hitBuffer && SameBytes(hits, state.lastHits);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(hits.size() * sizeof(RuntimeRayTracingHitRecord));
        const uint64_t bytes = keepHits ? 0 : graphics.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
        uint64_t available = state.Available(budget);
        if (bytes == UINT64_MAX || bytes >= available)
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
        auto top = std::make_unique<AccelerationStructure>();
        if (!top->BuildTopLevel(graphics, instances, available - bytes, true,
                                state.topLevel && state.lastInstances.size() == instances.size() ? state.topLevel.get()
                                                                                                 : nullptr))
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
        Microsoft::WRL::ComPtr<ID3D12Resource> upload;
        void* mapped = nullptr;
        if (!keepHits && !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(graphics.device, desc.Width, upload, &mapped)) {
            state.TrackRetirement(graphics, top->GetAllocationBytes());
            top->Reset(&graphics);
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
        }
        if (!keepHits) {
            std::memcpy(mapped, hits.data(), static_cast<size_t>(desc.Width));
            upload->Unmap(0, nullptr);
        }
        state.RetireTop(graphics, keepHits);
        state.topLevel = std::move(top);
        if (!keepHits) {
            state.hitBuffer = std::move(upload);
            state.hitBufferBytes = bytes;
        }
        state.lastHits = std::move(hits);
        state.lastInstances = std::move(instances);
        ++state.version;
    }
    state.sourceIdentity = source.surfaceRecords;
    state.sourceVersion = source.sourceVersion;
    state.layoutVersion = source.layoutVersion;
    state.materialVersion = materialVersion;
    state.sourceSurfaceCount = state.stats.sourceSurfaces;
    state.sourceRecordCount = source.surfaceRecords->size();
    state.materialSlotCount = materialSlots.size();
    state.materialCount = materials.size();
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
