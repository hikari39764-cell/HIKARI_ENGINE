#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingScene.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
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
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"
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
                for (const auto& bottom : bottomLevels) bytes += bottom->GetAllocationBytes();
                return bytes;
            }
            void Reset(const GFX::Context& graphics) {
                for (auto& bottom : bottomLevels) bottom->Reset(&graphics);
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
        bool SupportsGeometry(const GPUDRIVEN::GpuSceneSurfaceRecord& record) {
            return !record.skinned && !record.foliageWind && !record.waterSurface &&
                !record.sandSurface && !record.key.customVertexShader &&
                !record.key.materialFx && !record.key.transparent;
        }
        bool ValidTransform(const MATH::Mat4& world) {
            if (!MATH::IsFinite(world) || world.m[0][3] != 0 || world.m[1][3] != 0 ||
                world.m[2][3] != 0 || world.m[3][3] != 1) return false;
            const auto matrix = std::bit_cast<DirectX::XMFLOAT4X4>(world);
            const float determinant = DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&matrix)));
            return std::isfinite(determinant) && determinant != 0;
        }
        template <class T> bool SameBytes(const std::vector<T>& a, const std::vector<T>& b) {
            return a.size() == b.size() && (a.empty() ||
                std::memcmp(a.data(), b.data(), a.size()*sizeof(T)) == 0);
        }
    }

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

        void TrackRetirement(const GFX::Context& graphics, uint64_t bytes) {
            if (bytes == 0 || !graphics.frameFence ||
                graphics.frameFence->GetCompletedValue() >= graphics.currentFrameRetireFenceValue) return;
            if (!retiring.empty() && retiring.back().fence.Get() == graphics.frameFence &&
                retiring.back().value == graphics.currentFrameRetireFenceValue) retiring.back().bytes += bytes;
            else retiring.push_back({graphics.frameFence, graphics.currentFrameRetireFenceValue, bytes});
        }
        void RetireTop(const GFX::Context& graphics) {
            sourceIdentity = nullptr;
            TrackRetirement(graphics, hitBufferBytes + (topLevel ? topLevel->GetAllocationBytes() : 0));
            if (topLevel) topLevel->Reset(&graphics);
            topLevel.reset();
            GFX::RetireD3D12ObjectForFrame(hitBuffer, graphics, "RT.Scene.Hits");
            hitBufferBytes = 0;
            lastInstances.clear();
            lastHits.clear();
        }
        uint64_t Available(const RuntimeRayTracingBudget& budget) {
            stats.residentBytes = hitBufferBytes + (topLevel ? topLevel->GetAllocationBytes() : 0);
            for (const auto& [key, asset] : assets) stats.residentBytes += asset->Bytes();
            stats.retiringBytes = 0;
            for (const auto& entry : retiring) stats.retiringBytes += entry.bytes;
            const uint64_t used = stats.residentBytes + stats.retiringBytes;
            return used < budget.maximumBytes ? budget.maximumBytes-used : 0;
        }
        RuntimeRayTracingFrame Fail(const GFX::Context& graphics,
            const RuntimeRayTracingBudget& budget, RuntimeRayTracingStatus status) {
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
                source->metadataSrv.descriptorIndex < descriptorBegin || source->metadataSrv.descriptorIndex >= descriptorEnd) return nullptr;
            const uint64_t key = (uint64_t(handle.generation) << 32) | handle.index;
            auto& slot = assets[key];
            if (slot && (slot->sourceGeometry != resource || slot->metadata != source->metadataBuffer ||
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
                if (!CLUSTER::BuildClusterRayTracingChunks(*source, budget.trianglesPerChunk,
                    budget.rangesPerChunk, budget.chunksPerAsset, asset->chunks)) {
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
        bool CanReuse(const GPUDRIVEN::GpuDrivenSceneSource& source,
            const RuntimeRayTracingBudget& budget, RuntimeRayTracingMaterialVersion materials) {
            if (!topLevel || source.sourceVersion == 0 || materials.data == 0 || materials.bindings == 0 ||
                sourceIdentity != source.surfaceRecords || sourceVersion != source.sourceVersion ||
                layoutVersion != source.layoutVersion || materialVersion != materials || source.HasMassInstances()) return false;
            for (const auto& [key, asset] : assets) {
                const ClusterGeometryResourceHandle handle{uint32_t(key),uint32_t(key >> 32)};
                const auto* current = GetClusterGeometryResource(handle);
                if (!current || !current->ready || current->sourceRevision != asset->sourceRevision ||
                    current->metadataBuffer != asset->metadata || GetRenderResourcePool().GetResource(handle) != asset->sourceGeometry ||
                    current->srv.descriptorIndex != asset->geometryDescriptor || current->metadataSrv.descriptorIndex != asset->metadataDescriptor ||
                    budget.trianglesPerChunk != asset->trianglesPerChunk || budget.rangesPerChunk != asset->rangesPerChunk ||
                    asset->chunks.size() > budget.chunksPerAsset) return false;
            }
            Available(budget);
            return lastHits.size() <= budget.maximumHitRecords && lastInstances.size() <= budget.maximumInstances &&
                stats.residentBytes + stats.retiringBytes <= budget.maximumBytes;
        }
        RuntimeRayTracingFrame Publish(const GFX::Context& graphics, const RuntimeRayTracingBudget& budget) {
            stats.status = RuntimeRayTracingStatus::Ready;
            stats.instances = static_cast<uint32_t>(lastInstances.size());
            stats.hitRecords = static_cast<uint32_t>(lastHits.size());
            stats.sharedAssets = static_cast<uint32_t>(assets.size());
            stats.readyChunks = 0;
            for (const auto& [key, asset] : assets) stats.readyChunks += static_cast<uint32_t>(asset->bottomLevels.size());
            Available(budget);
            return {topLevel->GetGpuAddress(), hitBuffer->GetGPUVirtualAddress(),
                stats.hitRecords, version, graphics.currentFrameRetireFenceValue, true, false,
                graphics.frameFence, graphics.deviceGeneration, sourceIdentity, layoutVersion, materialVersion};
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
        state.converter.Reset(graphics);
        state.stats = {};
        state.device = nullptr;
    }

    RuntimeRayTracingFrame RuntimeRayTracingScene::Prepare(const GFX::Context& graphics,
        const GPUDRIVEN::GpuDrivenSceneSource& source, std::span<const uint32_t> materialSlots,
        std::span<const MATERIAL::GpuMaterialData> materials, const RuntimeRayTracingBudget& budget,
        RuntimeRayTracingMaterialVersion materialVersion) {
        auto& state = *state_;
        state.stats = {};
        if (!graphics.device || !graphics.cmdList || !graphics.frameFence || !graphics.deferredReleaseQueue ||
            graphics.currentFrameRetireFenceValue == 0 || !source.surfaceRecords ||
            budget.buildsPerFrame == 0 || budget.geometryUploadsPerFrame == 0 || budget.maximumBytes == 0 ||
            budget.maximumInstances == 0 || budget.maximumInstances > (1u << 24) ||
            budget.maximumHitRecords == 0 || budget.maximumHitRecords > (1u << 24))
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
        }
        std::erase_if(state.retiring, [](const auto& entry) {
            return entry.fence->GetCompletedValue() >= entry.value;
        });
        if (!state.supportsQueries) return state.Fail(graphics, budget, RuntimeRayTracingStatus::UnsupportedDevice);
        if (materialSlots.size()==state.materialSlotCount && materials.size()==state.materialCount &&
            source.surfaceRecords->size()==state.sourceRecordCount && state.CanReuse(source, budget, materialVersion)) {
            state.stats.reusedTopLevel = true;
            state.stats.sourceSurfaces = state.sourceSurfaceCount;
            return state.Publish(graphics, budget);
        }
        const auto& records = *source.surfaceRecords;
        // 未対応の scene は geometry 解決/グループ割当より先に判定する。
        // fallback 中も毎フレーム BLAS 用の CPU 作業を繰り返さない。
        for (size_t i = 0; i < records.size(); ++i) {
            const auto& record = records[i];
            if (!record.visible) continue;
            ++state.stats.sourceSurfaces;
            if (!SupportsGeometry(record)) { ++state.stats.unsupportedSurfaces; continue; }
            if (!record.valid || !ValidTransform(record.clusterWorldMatrix) ||
                i >= materialSlots.size() || materialSlots[i] >= materials.size()) {
                ++state.stats.missingSurfaces; continue;
            }
            const auto* program = MATERIAL::GetRuntimeMaterialProgramRegistry().Find({materials[materialSlots[i]].materialProgramIndex});
            if (!program || program->capabilities.usesScreen || program->capabilities.usesDerivatives) {
                ++state.stats.unsupportedSurfaces; continue;
            }
        }
        if (source.HasMassInstances()) ++state.stats.unsupportedSurfaces;
        state.stats.sharedAssets = static_cast<uint32_t>(state.assets.size());
        if (state.stats.unsupportedSurfaces || state.stats.missingSurfaces)
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::IncompleteScene);
        ++state.serial;
        std::map<GroupKey, Group> groups;
        for (size_t i = 0; i < records.size(); ++i) {
            const auto& record = records[i];
            if (!record.visible) continue;
            const auto handle = record.key.resources.clusterGeometry;
            Asset* asset = state.ResolveAsset(graphics, budget, handle);
            const auto* range = asset ? FindClusterGeometrySurfaceRange(handle,
                record.nodeIndex, record.meshIndex, record.primitiveIndex) : nullptr;
            if (!range || range->surfaceIndex >= asset->source->surfaceRanges.size()) {
                ++state.stats.missingSurfaces; continue;
            }
            std::array<float, 16> matrix{};
            std::memcpy(matrix.data(), &record.clusterWorldMatrix, sizeof(matrix));
            const uint64_t assetKey = (uint64_t(handle.generation) << 32) | handle.index;
            auto& group = groups[{record.objectId.value, record.objectLifetimeGeneration, assetKey, matrix}];
            if (!group.asset) {
                group.asset = asset;
                group.world = record.clusterWorldMatrix;
                group.surfaces.resize(asset->source->surfaceRanges.size(), {0xffffffffu, 0u});
            }
            auto& surface = group.surfaces[range->surfaceIndex];
            // 同じ物体/transform/surface の競合を黙って上書きしない。
            if (surface.first != 0xffffffffu && surface.first != materialSlots[i]) ++state.stats.missingSurfaces;
            surface = {materialSlots[i], (record.key.doubleSided ? 1u : 0u) | (record.castShadow ? 2u : 0u)};
        }
        for (auto it = state.assets.begin(); it != state.assets.end();) {
            if (it->second->touched == state.serial) { ++it; continue; }
            state.RetireTop(graphics);
            state.TrackRetirement(graphics, it->second->Bytes());
            it->second->Reset(graphics);
            it = state.assets.erase(it);
        }
        state.stats.sharedAssets = static_cast<uint32_t>(state.assets.size());
        // Missing/unsupported surfaces must never turn into falsely unoccluded GI rays.
        if (state.stats.unsupportedSurfaces || state.stats.missingSurfaces)
            return state.Fail(graphics, budget, RuntimeRayTracingStatus::IncompleteScene);
        if (groups.empty()) {
            state.RetireTop(graphics);
            state.Available(budget);
            state.stats.status = RuntimeRayTracingStatus::Ready;
            return {0, 0, 0, ++state.version, graphics.currentFrameRetireFenceValue, true, true,
                graphics.frameFence, graphics.deviceGeneration, source.surfaceRecords, source.layoutVersion, materialVersion};
        }
        for (auto& [key, asset] : state.assets) {
            if (!asset->geometry.indices && state.frameUploads < budget.geometryUploadsPerFrame) {
                if (!state.converter.Build(graphics, *asset->source, asset->sourceGeometry,
                    state.Available(budget), asset->geometry))
                    return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
                ++state.stats.geometryUploads;
                ++state.frameUploads;
            }
            while (asset->geometry.indices && asset->bottomLevels.size() < asset->chunks.size() &&
                state.frameBuilds < budget.buildsPerFrame) {
                const auto& chunk = asset->chunks[asset->bottomLevels.size()];
                std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> descriptions(chunk.ranges.size());
                for (size_t r = 0; r < chunk.ranges.size(); ++r)
                    if (!asset->geometry.DescribeTriangles(chunk.ranges[r].firstPrimitive,
                        chunk.ranges[r].primitiveCount, descriptions[r]))
                        return state.Fail(graphics, budget, RuntimeRayTracingStatus::InvalidInput);
                auto bottom = std::make_unique<AccelerationStructure>();
                if (!bottom->BuildBottomLevel(graphics, descriptions, state.Available(budget)))
                    return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
                asset->bottomLevels.push_back(std::move(bottom));
                ++state.stats.builtChunks;
                ++state.frameBuilds;
            }
            state.stats.readyChunks += static_cast<uint32_t>(asset->bottomLevels.size());
            state.stats.pendingChunks += static_cast<uint32_t>(asset->chunks.size()-asset->bottomLevels.size());
        }
        if (state.stats.pendingChunks) return state.Fail(graphics, budget, RuntimeRayTracingStatus::Building);
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
        std::vector<RuntimeRayTracingHitRecord> hits;
        for (const auto& [key, group] : groups) {
            const auto& asset = *group.asset;
            for (size_t c = 0; c < asset.chunks.size(); ++c) {
                const auto& chunk = asset.chunks[c];
                if (std::none_of(chunk.ranges.begin(), chunk.ranges.end(), [&](const auto& range) {
                    return group.surfaces[range.surfaceIndex].first != 0xffffffffu;
                })) continue;
                if (instances.size() >= budget.maximumInstances || chunk.ranges.size() > budget.maximumHitRecords-hits.size())
                    return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
                D3D12_RAYTRACING_INSTANCE_DESC instance{};
                for (int row = 0; row < 3; ++row)
                    for (int col = 0; col < 4; ++col) instance.Transform[row][col] = group.world.m[col][row];
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
        if (SameBytes(instances, state.lastInstances) && SameBytes(hits, state.lastHits) && state.topLevel)
            state.stats.reusedTopLevel = true;
        else {
            const auto desc = CD3DX12_RESOURCE_DESC::Buffer(hits.size()*sizeof(RuntimeRayTracingHitRecord));
            const uint64_t bytes = graphics.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
            uint64_t available = state.Available(budget);
            if (bytes == UINT64_MAX || bytes >= available)
                return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
            auto top = std::make_unique<AccelerationStructure>();
            if (!top->BuildTopLevel(graphics, instances, available-bytes, true,
                state.topLevel && state.lastInstances.size() == instances.size() ? state.topLevel.get() : nullptr))
                return state.Fail(graphics, budget, RuntimeRayTracingStatus::BudgetExceeded);
            Microsoft::WRL::ComPtr<ID3D12Resource> upload;
            void* mapped = nullptr;
            if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(graphics.device, desc.Width, upload, &mapped)) {
                state.TrackRetirement(graphics, top->GetAllocationBytes());
                top->Reset(&graphics);
                return state.Fail(graphics, budget, RuntimeRayTracingStatus::BuildFailed);
            }
            std::memcpy(mapped, hits.data(), static_cast<size_t>(desc.Width));
            upload->Unmap(0, nullptr);
            state.RetireTop(graphics);
            state.topLevel = std::move(top);
            state.hitBuffer = std::move(upload);
            state.hitBufferBytes = bytes;
            state.lastHits = std::move(hits);
            state.lastInstances = std::move(instances);
            ++state.version;
        }
        state.sourceIdentity = source.surfaceRecords;
        state.sourceVersion = source.sourceVersion;
        state.layoutVersion = source.layoutVersion;
        state.materialVersion = materialVersion;
        state.sourceSurfaceCount = state.stats.sourceSurfaces;
        state.sourceRecordCount=source.surfaceRecords->size();
        state.materialSlotCount=materialSlots.size(); state.materialCount=materials.size();
        return state.Publish(graphics, budget);
    }

    bool IsRuntimeRayTracingFrameCurrent(const RuntimeRayTracingFrame& frame,
        const GFX::Context& graphics, RuntimeRayTracingMaterialVersion materials) noexcept {
        return graphics.device && graphics.cmdList && graphics.srvHeap && graphics.frameFence &&
            graphics.deviceGeneration && graphics.currentFrameRetireFenceValue && frame.complete && frame.sceneIdentity &&
            frame.retireFenceValue==graphics.currentFrameRetireFenceValue && frame.frameFence==graphics.frameFence &&
            frame.deviceGeneration==graphics.deviceGeneration && frame.materialVersion==materials &&
            (frame.empty || (frame.accelerationStructure && frame.hitRecords && frame.hitRecordCount));
    }
}
