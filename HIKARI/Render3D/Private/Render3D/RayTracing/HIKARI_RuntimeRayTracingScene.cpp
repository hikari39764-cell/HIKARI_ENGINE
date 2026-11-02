#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingScene.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <tuple>
#include <vector>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_FencedUploadBuffers.h"
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
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingInputs.h"
#include "RenderCore/Resources/HIKARI_RenderResourceSystem.h"

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
    uint32_t index = 0;                                  // instance を並べた順の番号（GroupUse の位置）
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
template <class T>
bool SameBytes(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}
void WriteTransform(D3D12_RAYTRACING_INSTANCE_DESC& instance, const MATH::Mat4& world) {
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col)
            instance.Transform[row][col] = world.m[col][row];
}
bool SameMatrix(const MATH::Mat4& a, const MATH::Mat4& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

// まだ場面へ入れられない面（未解決・組み途中）。残っている間は毎フレーム組み直す。
inline constexpr uint32_t kUnsettledExclusions = RuntimeRayTracingExclusionBit(RuntimeRayTracingExclusion::Missing) |
                                                 RuntimeRayTracingExclusionBit(RuntimeRayTracingExclusion::Pending);
// 表面の前回の入れ方。group は静的な群の番号。変形する面は kDeformedGroup、入れなかった面は kNoGroup。
inline constexpr uint32_t kNoGroup = 0xffffffffu;
inline constexpr uint32_t kDeformedGroup = 0xfffffffeu;
// materialSlot は hit 記録に書いた材質の slot、surfaceIndex は群の中の面の番号（静的な群の面だけ）。
struct RecordUse {
    RayRecordInputs inputs{};
    uint32_t group = kNoGroup;
    uint32_t materialSlot = 0xffffffffu;
    uint32_t surfaceIndex = 0;
};
// 静的な群の前回の行列、lastInstances の中の instance の範囲と lastHits の中の hit 記録の先頭、面ごとの
// （材質 slot, 旗）。同じ物体・形状の群は GroupKey の順で隣り合う。
struct GroupUse {
    uint64_t object = 0, lifetime = 0, assetKey = 0;
    MATH::Mat4 world{};
    uint32_t firstInstance = 0, instanceCount = 0;
    const Asset* asset = nullptr;
    uint32_t firstHit = 0;
    std::vector<std::pair<uint32_t, uint32_t>> surfaces;
};
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
    // hit 記録。材質の slot だけが替わった時は GPU が読み終えた予備へ書き直す。
    GFX::FencedUploadBuffers hitUploads;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> lastInstances;
    std::vector<RuntimeRayTracingHitRecord> lastHits;
    std::vector<RetiringBytes> retiring;
    RuntimeRayTracingStats stats;
    // 最後に組んだ時の入れなかった面（大量配置は毎フレーム足す）。加速構造を使い回すフレームも同じ値を出す。
    RuntimeRayTracingExclusions lastExcluded{};
    // この場面が一度欠けなく揃ったか（失敗と場面の入れ替えで戻る）。揃った後に足した面だけを Pending として遅らせる
    // （初めの読み込み中は揃うまで待つ。欠けた場面で GI を始めると光線が抜けて空の光を拾う）。
    bool complete = false;
    std::map<DeformedKey, Deformed> deformed;
    bool deformedRefreshed = false; // 直前の DeformAndRefit が頂点を書き直した（TLAS の refit が要る）
    RuntimeRayTracingDeformation deformation;
    RuntimeRayTracingChanges changes;
    // 変形の compute が使えない（shader の失敗など）時は蒙皮を外す面に戻す。
    bool deformationBroken = false;
    // 前回の全体の組み立てで決めた表面と群の入れ方。配置だけが動いたフレームはこれで instance の変換だけを直す。
    std::vector<RecordUse> recordUses;
    std::vector<GroupUse> groupUses;
    bool terrainBound = false;
    // UpdateInPlace の作業領域（毎フレーム確保し直さない）。groupStates は 0: 未確認, 1: 同じ, 2: 動いた。
    // groupRetints は面の材質 slot が替わった群（hit 記録を書き直す）。
    std::vector<MATH::Mat4> movedWorlds;
    std::vector<uint8_t> groupStates;
    std::vector<uint8_t> groupRetints;

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
        TrackRetirement(graphics, (keepHits ? 0 : hitUploads.AllocationBytes()) +
                                      (topLevel ? topLevel->GetAllocationBytes() : 0));
        if (topLevel)
            topLevel->Reset(&graphics);
        topLevel.reset();
        if (!keepHits) {
            hitUploads.Reset(graphics, "RT.Scene.Hits");
            lastHits.clear();
        }
        lastInstances.clear();
    }
    uint64_t Available(const RuntimeRayTracingBudget& budget) {
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
    RuntimeRayTracingFrame Fail(const GFX::Context& graphics, const RuntimeRayTracingBudget& budget,
                                RuntimeRayTracingStatus status) {
        complete = false;
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
    // 加速構造の形（表面の並び・形状の資源・予算）が前回のままか。配置（sourceVersion）と材質の値（版）が
    // 変わったかは見ない。それは UpdateInPlace が表面ごとに確かめる。
    bool SameStructure(const GPUDRIVEN::GpuDrivenSceneSource& source, const RuntimeRayTracingBudget& budget,
                       RuntimeRayTracingMaterialVersion materials) {
        if (!topLevel || source.sourceVersion == 0 || materials.data == 0 || materials.bindings == 0 ||
            sourceIdentity != source.surfaceRecords || layoutVersion != source.layoutVersion ||
            (lastExcluded.mask & kUnsettledExclusions) != 0u)
            return false;
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
    // 配置か材質だけが変わったフレーム。表面の入れ方が前回と同じなら群をまとめ直さず、動いた群の instance の変換を
    // 書き換えて TLAS をその場で組み直し、材質の slot が替わった群は hit 記録の材質番号だけを書き直す（資源は作り
    // 直さない。材質の値は版だけ進める）。群が割れる・重なる、行列が不正になるなど全体の組み直しと結果が変わる時は
    // nullopt を返し、呼び出し側が全体を組み直す。
    std::optional<RuntimeRayTracingFrame> UpdateInPlace(const GFX::Context& graphics,
                                                        const GPUDRIVEN::GpuDrivenSceneSource& source,
                                                        std::span<const uint32_t> materialSlots,
                                                        std::span<const MATERIAL::GpuMaterialData> materials,
                                                        RuntimeRayTracingMaterialVersion currentMaterials,
                                                        const RuntimeRayTracingBudget& budget,
                                                        const RuntimeRayTracingSurfaceBindings& surfaces) {
        const auto& records = *source.surfaceRecords;
        if (recordUses.size() != records.size() ||
            terrainBound != (surfaces.terrainRecords != 0 && surfaces.surfaceFloats != 0))
            return std::nullopt;
        groupStates.assign(groupUses.size(), 0);
        groupRetints.assign(groupUses.size(), 0);
        movedWorlds.resize(groupUses.size());
        for (size_t i = 0; i < records.size(); ++i) {
            const auto& record = records[i];
            auto& use = recordUses[i];
            if (!(ReadRayRecordInputs(record, i, materialSlots, materials) == use.inputs))
                return std::nullopt;
            const MATH::Mat4& world = record.clusterWorldMatrix;
            const uint32_t slot = i < materialSlots.size() ? materialSlots[i] : 0xffffffffu;
            if (use.group == kDeformedGroup) {
                // 変形する面は DeformAndRefit が毎回行列を読む。不正な行列と材質替えは全体の組み直しで扱う。
                if (slot != use.materialSlot || !IsValidRayTransform(world))
                    return std::nullopt;
                continue;
            }
            // 入れなかった面は、材質プログラムが同じなら入れ方も同じ（inputs で確かめた）。
            if (use.group == kNoGroup)
                continue;
            if (slot != use.materialSlot) {
                groupUses[use.group].surfaces[use.surfaceIndex].first = slot;
                use.materialSlot = slot;
                groupRetints[use.group] = 1;
            }
            uint8_t& state = groupStates[use.group];
            if (state == 0) {
                const bool same = SameMatrix(world, groupUses[use.group].world);
                if (!same && !IsValidRayTransform(world))
                    return std::nullopt;
                state = same ? 1 : 2;
                movedWorlds[use.group] = world;
            } else if (!SameMatrix(world, movedWorlds[use.group])) {
                return std::nullopt; // 同じ群の面が別々に動いた（全体では群が割れる）
            }
        }
        bool moved = false;
        for (size_t g = 0; g < groupUses.size(); ++g) {
            if (groupStates[g] == 0)
                return std::nullopt;
            if (groupStates[g] != 2)
                continue;
            moved = true;
            // 同じ物体・形状の別の群と行列が重なれば、全体の組み直しでは一つの群にまとまる。
            const auto sameOwner = [&](size_t n) {
                return groupUses[n].object == groupUses[g].object && groupUses[n].lifetime == groupUses[g].lifetime &&
                       groupUses[n].assetKey == groupUses[g].assetKey;
            };
            for (size_t n = g; n-- > 0 && sameOwner(n);)
                if (SameMatrix(movedWorlds[n], movedWorlds[g]))
                    return std::nullopt;
            for (size_t n = g + 1; n < groupUses.size() && sameOwner(n); ++n)
                if (SameMatrix(movedWorlds[n], movedWorlds[g]))
                    return std::nullopt;
        }
        // 変形する面を先に refit する（TLAS はそれを含めて組み直すか refit する）。
        if (!deformed.empty() && !DeformAndRefit(graphics, source, budget, surfaces)) {
            deformationBroken = true;
            return Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
        }
        if (moved) {
            for (size_t g = 0; g < groupUses.size(); ++g) {
                if (groupStates[g] != 2)
                    continue;
                auto& group = groupUses[g];
                group.world = movedWorlds[g];
                for (uint32_t k = 0; k < group.instanceCount; ++k)
                    WriteTransform(lastInstances[group.firstInstance + k], group.world);
            }
            if (!topLevel->RebuildTopLevelInPlace(graphics, lastInstances)) {
                lastInstances.clear(); // 書き換えた記述を、組まれた TLAS と取り違えないようにする
                return std::nullopt;
            }
        } else if (deformedRefreshed && !topLevel->RefitInPlace(graphics)) {
            deformationBroken = true;
            return Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
        }
        // hit 記録は全体の組み立てと同じ並び（使う塊の範囲の順）で、面ごとの材質から書き直す。
        bool retinted = false;
        for (size_t g = 0; g < groupUses.size(); ++g) {
            if (!groupRetints[g])
                continue;
            retinted = true;
            const auto& group = groupUses[g];
            uint32_t hit = group.firstHit;
            for (const auto& chunk : group.asset->chunks) {
                if (std::none_of(chunk.ranges.begin(), chunk.ranges.end(), [&](const auto& range) {
                        return group.surfaces[range.surfaceIndex].first != 0xffffffffu;
                    }))
                    continue;
                for (const auto& range : chunk.ranges)
                    lastHits[hit++].materialIndex = group.surfaces[range.surfaceIndex].first;
            }
        }
        if (retinted &&
            !hitUploads.Write(graphics, lastHits.data(), lastHits.size() * sizeof(lastHits[0]), "RT.Scene.Hits")) {
            lastHits.clear(); // 書き換えた記録を、GPU にある記録と取り違えないようにする
            return std::nullopt;
        }
        if (moved || retinted)
            ++version;
        sourceVersion = source.sourceVersion;
        materialVersion = currentMaterials;
        stats.reusedTopLevel = true;
        stats.sourceSurfaces = sourceSurfaceCount;
        ExcludeMassInstances(source);
        return Publish(graphics, budget);
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
        state.SameStructure(source, budget, materialVersion)) {
        // 配置か材質の値が変わっただけなら、群をまとめ直さず instance の変換と材質の版だけを直す
        // （できなければ下で全体を組み直す）。
        if (state.sourceVersion != source.sourceVersion || state.materialVersion != materialVersion) {
            if (auto updated =
                    state.UpdateInPlace(graphics, source, materialSlots, materials, materialVersion, budget, surfaces))
                return *updated;
        } else {
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
    state.ExcludeMassInstances(source);
    if ((state.lastExcluded.mask & kUnsettledExclusions) == 0u)
        state.complete = true;
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
        state.ExcludeMassInstances(source);
    }
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
                hits.push_back({asset.geometryDescriptor, asset.metadataDescriptor, material, flags,
                                range.firstPrimitive, range.firstCluster, range.clusterCount, range.primitiveCount});
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
    if (SameBytes(instances, state.lastInstances) && SameBytes(hits, state.lastHits) && state.topLevel) {
        state.stats.reusedTopLevel = true;
        // 変形した BLAS は中身が動いたので、それを指す TLAS も refit する。
        if (!state.deformed.empty() && !state.topLevel->RefitInPlace(graphics))
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
    } else {
        const bool keepHits = state.hitUploads.Current() && SameBytes(hits, state.lastHits);
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
