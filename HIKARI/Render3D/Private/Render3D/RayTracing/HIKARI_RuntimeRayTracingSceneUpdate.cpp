#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingSceneState.h"

#include <algorithm>
#include <d3dx12.h>

#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_MassGpuScene.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteUpload.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Lighting/HIKARI_LightmapRuntime.h"
#include "RenderCore/Resources/HIKARI_RenderResourceSystem.h"

// 加速構造の形が前回のままのフレーム：配置・材質・変形だけを、資源を作り直さずにその場で直す。
namespace HIKARI::RENDER3D::RAYTRACING {
using namespace INTERNAL;
bool RuntimeRayTracingScene::State::DeformAndRefit(const GFX::Context& graphics,
                                                   const GPUDRIVEN::GpuDrivenSceneSource& source,
                                                   const RuntimeRayTracingBudget& budget,
                                                   const RuntimeRayTracingSurfaceBindings& surfaces) {
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
                if (!asset.geometry.DescribeTriangles(chunk.ranges[r].firstPrimitive, chunk.ranges[r].primitiveCount,
                                                      descriptions[r], pool + entry.job.outputBase))
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
bool RuntimeRayTracingScene::State::SameStructure(const GPUDRIVEN::GpuDrivenSceneSource& source,
                                                  std::span<const uint32_t> massMaterialSlots,
                                                  const RuntimeRayTracingBudget& budget,
                                                  RuntimeRayTracingMaterialVersion materials) {
    if (!topLevel || source.sourceVersion == 0 || materials.data == 0 || materials.bindings == 0 ||
        lightmapVersion != LIGHTMAP::GetVersion() || sourceIdentity != source.surfaceRecords ||
        layoutVersion != source.layoutVersion || (lastExcluded.mask & kUnsettledExclusions) != 0u ||
        !mass.Matches(source.mass, massMaterialSlots))
        return false;
    for (const auto& [key, asset] : assets) {
        const ClusterGeometryResourceHandle handle{uint32_t(key), uint32_t(key >> 32)};
        const auto* current = GetClusterGeometryResource(handle);
        if (!current || !current->ready || current->sourceRevision != asset->sourceRevision ||
            current->metadataBuffer != asset->metadata ||
            GetRenderResourcePool().GetResource(handle) != asset->sourceGeometry ||
            current->srv.descriptorIndex != asset->geometryDescriptor ||
            current->metadataSrv.descriptorIndex != asset->metadataDescriptor ||
            budget.trianglesPerChunk != asset->trianglesPerChunk || budget.rangesPerChunk != asset->rangesPerChunk ||
            asset->chunks.size() > budget.chunksPerAsset)
            return false;
    }
    Available(budget);
    return lastHits.size() <= budget.maximumHitRecords && lastInstances.size() <= budget.maximumInstances &&
           stats.residentBytes <= budget.maximumBytes; // 解放待ちは数フレームで消えるので数えない
}
bool RuntimeRayTracingScene::State::CanRetargetProxies(std::span<const RuntimeRayTracingProxy> proxies) const {
    if (proxies.size() != proxySurfaceCounts.size() || proxyFirstInstance + proxies.size() > lastInstances.size())
        return false;
    size_t hits = proxyFirstHit;
    for (size_t i = 0; i < proxies.size(); ++i) {
        if (proxies[i].bottomLevel == 0 || proxies[i].surfaces.size() != proxySurfaceCounts[i])
            return false;
        hits += proxies[i].surfaces.size();
    }
    return hits <= lastHits.size();
}
bool RuntimeRayTracingScene::State::RetargetProxies(std::span<const RuntimeRayTracingProxy> proxies) {
    bool hitsChanged = false;
    size_t hit = proxyFirstHit;
    for (size_t i = 0; i < proxies.size(); ++i) {
        lastInstances[proxyFirstInstance + i].AccelerationStructure = proxies[i].bottomLevel;
        for (const uint32_t surface : proxies[i].surfaces) {
            hitsChanged = hitsChanged || lastHits[hit].reserved != surface;
            lastHits[hit++].reserved = surface;
        }
    }
    return hitsChanged;
}
std::optional<RuntimeRayTracingFrame> RuntimeRayTracingScene::State::UpdateInPlace(
    const GFX::Context& graphics, const GPUDRIVEN::GpuDrivenSceneSource& source,
    std::span<const uint32_t> materialSlots, std::span<const MATERIAL::GpuMaterialData> materials,
    RuntimeRayTracingMaterialVersion currentMaterials, const RuntimeRayTracingBudget& budget,
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
    // 大量配置の動いた個体：compute が使えれば GPU で記述を書き（CPU は変換を書かない）、使えなければ CPU で書き直す
    // （並びが合わなければ全体を組み直す）。
    const bool massOnGpu = mass.Included() && surfaces.massRecords != 0 && mass.CanDispatch(graphics);
    const bool proxiesChanged = surfaces.proxyVersion != proxyVersion;
    // lastInstances の大量配置の分が古い時は、CPU の写しから TLAS を組み直せない。
    if (!massOnGpu && massDescsOnGpu && (moved || proxiesChanged || mass.Moved(source.mass)))
        return std::nullopt;
    const std::optional<bool> massMoved =
        massOnGpu ? std::optional<bool>(mass.Moved(source.mass)) : mass.Update(source.mass, lastInstances);
    if (!massMoved)
        return std::nullopt;
    // 変形する面を先に refit する（TLAS はそれを含めて組み直すか refit する）。
    if (!deformed.empty() && !DeformAndRefit(graphics, source, budget, surfaces)) {
        deformationBroken = true;
        return Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
    }
    // 代理の BLAS は同じ番地のまま中身が変わることもあるので、版が進んだら TLAS は必ず組み直す。
    const bool proxyHitsChanged = proxiesChanged && RetargetProxies(surfaces.proxies);
    if (moved || *massMoved || proxiesChanged) {
        for (size_t g = 0; g < groupUses.size(); ++g) {
            if (groupStates[g] != 2)
                continue;
            auto& group = groupUses[g];
            group.world = movedWorlds[g];
            for (uint32_t k = 0; k < group.instanceCount; ++k)
                WriteTransform(lastInstances[group.firstInstance + k], group.world);
        }
        const bool rebuilt =
            massOnGpu ? RebuildFromGpuInstances(graphics, source, surfaces.massRecords, moved || proxiesChanged)
                      : topLevel->RebuildTopLevelInPlace(graphics, lastInstances);
        if (!rebuilt) {
            lastInstances.clear(); // 書き換えた記述を、組まれた TLAS と取り違えないようにする
            if (proxyHitsChanged)
                lastHits.clear();
            gpuPrefixValid = false;
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
    if ((retinted || proxyHitsChanged) &&
        !hitUploads.Write(graphics, lastHits.data(), lastHits.size() * sizeof(lastHits[0]), "RT.Scene.Hits")) {
        lastHits.clear(); // 書き換えた記録を、GPU にある記録と取り違えないようにする
        return std::nullopt;
    }
    if (moved || *massMoved || retinted || proxiesChanged)
        ++version;
    proxyVersion = surfaces.proxyVersion;
    sourceVersion = source.sourceVersion;
    materialVersion = currentMaterials;
    stats.reusedTopLevel = true;
    stats.sourceSurfaces = sourceSurfaceCount;
    stats.excluded = lastExcluded;
    return Publish(graphics, budget);
}
bool RuntimeRayTracingScene::State::RebuildFromGpuInstances(const GFX::Context& graphics,
                                                            const GPUDRIVEN::GpuDrivenSceneSource& source,
                                                            D3D12_GPU_VIRTUAL_ADDRESS massRecords, bool prefixChanged) {
    auto* commands = graphics.cmdList;
    const uint64_t bytes = lastInstances.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    if (!source.mass || commands == nullptr || bytes == 0)
        return false;
    if (gpuInstanceBytes < bytes) {
        TrackRetirement(graphics, gpuInstanceBytes);
        GFX::RetireD3D12ObjectForFrame(gpuInstances, graphics, "RT.Scene.GpuInstances");
        gpuInstanceBytes = 0;
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (FAILED(GFX::VIDEO_MEMORY::CreateCommittedResource(
                graphics.device, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                GFX::VIDEO_MEMORY::Owner::Geometry, IID_PPV_ARGS(gpuInstances.GetAddressOf()))))
            return false;
        gpuInstanceBytes = bytes;
        gpuInstanceState = D3D12_RESOURCE_STATE_COMMON;
        gpuPrefixValid = false;
    }
    const auto transition = [&](D3D12_RESOURCE_STATES next) {
        if (gpuInstanceState == next)
            return;
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(gpuInstances.Get(), gpuInstanceState, next);
        commands->ResourceBarrier(1, &barrier);
        gpuInstanceState = next;
    };
    const uint64_t prefixBytes = uint64_t(mass.FirstInstance()) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    if (prefixBytes != 0 && (!gpuPrefixValid || prefixChanged)) {
        if (!gpuPrefixUploads.Write(graphics, lastInstances.data(), prefixBytes, "RT.Scene.GpuInstancePrefix"))
            return false;
        transition(D3D12_RESOURCE_STATE_COPY_DEST);
        commands->CopyBufferRegion(gpuInstances.Get(), 0, gpuPrefixUploads.Current(), 0, prefixBytes);
    }
    gpuPrefixUploads.MarkRead(graphics);
    gpuPrefixValid = true;
    transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!mass.Dispatch(graphics, *source.mass, massRecords, gpuInstances->GetGPUVirtualAddress()))
        return false;
    const auto written = CD3DX12_RESOURCE_BARRIER::UAV(gpuInstances.Get());
    commands->ResourceBarrier(1, &written);
    transition(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (!topLevel->RebuildTopLevelInPlace(graphics, gpuInstances->GetGPUVirtualAddress(),
                                          static_cast<uint32_t>(lastInstances.size())))
        return false;
    massDescsOnGpu = true;
    return true;
}
} // namespace HIKARI::RENDER3D::RAYTRACING
