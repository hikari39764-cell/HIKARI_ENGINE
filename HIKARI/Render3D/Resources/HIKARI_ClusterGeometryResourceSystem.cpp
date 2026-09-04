#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Windows.h>
#include <d3dx12.h>

#include "Assets/Geometry/HIKARI_HcmeshFormat.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Numeric/HIKARI_IntegerConversion.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_RenderResourceSystem.h"
#include "Render3D/Resources/Internal/HIKARI_ClusterGeometrySourceIdentity.h"

namespace HIKARI::RENDER3D {

    namespace {
        constexpr uint32_t kClusterGeometryDescriptorCount =
            GFX::DESCRIPTOR::kClusterGeometryDescriptorCountPerResource;
        constexpr const char* kClusterGeometryDescriptorOwner =
            "ClusterGeometryResourceSystem";

        struct ClusterGeometryMemorySource {
            CLUSTER::ClusterGeometryPackedBytes packed{};
            uint64_t revision = 0;
        };

        struct ClusterGeometryResourceSystemState {
            GFX::Context context{};
            std::unordered_map<std::string, ClusterGeometryResourceRecord> recordsBySourceKey{};
            std::unordered_map<uint64_t, std::string> sourceKeyByHandle{};
            std::unordered_map<std::string, ClusterGeometryMemorySource>
                memorySourcesBySourceKey{};
            ClusterGeometryResourceSystemStats stats{};
            std::atomic<uint32_t> ownerThreadId{ 0 };
            std::atomic<uint32_t> ownerThreadViolations{ 0 };
            uint64_t nextMemorySourceRevision = 1;
            ID3D12Device* uploadDevice = nullptr;
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> uploadAllocator{};
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uploadCommandList{};
            Microsoft::WRL::ComPtr<ID3D12Fence> uploadFence{};
            HANDLE uploadFenceEvent = nullptr;
            uint64_t uploadFenceValue = 1;
            bool contextMismatchReported = false;
        };

        ClusterGeometryResourceSystemState& State() {
            static ClusterGeometryResourceSystemState state{};
            return state;
        }

        bool IsContextCompatible(
            const GFX::Context& owner,
            const GFX::Context& candidate) noexcept {

            return owner.device == nullptr ||
                (owner.deviceGeneration == candidate.deviceGeneration &&
                 owner.device == candidate.device &&
                 owner.srvHeap == candidate.srvHeap);
        }

        uint64_t PackHandle(ClusterGeometryResourceHandle handle) {
            return (static_cast<uint64_t>(handle.generation) << 32) |
                static_cast<uint64_t>(handle.index);
        }

        struct ClusterGeometryUploadBuffer {
            Microsoft::WRL::ComPtr<ID3D12Resource> gpu{};
            Microsoft::WRL::ComPtr<ID3D12Resource> upload{};
            UINT64 byteSize = 0;
        };

        uint64_t NextMemorySourceRevision(
            ClusterGeometryResourceSystemState& state) noexcept {

            const uint64_t revision = state.nextMemorySourceRevision++;
            if (state.nextMemorySourceRevision == 0) {
                state.nextMemorySourceRevision = 1;
            }
            return revision != 0 ? revision : state.nextMemorySourceRevision++;
        }

        bool EnterOwnerThread(
            ClusterGeometryResourceSystemState& state) noexcept {

            const uint32_t current = GetCurrentThreadId();
            uint32_t owner = state.ownerThreadId.load(
                std::memory_order_acquire);
            if (owner == 0u && state.ownerThreadId.compare_exchange_strong(
                    owner,
                    current,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return true;
            }
            if (owner == 0u) {
                owner = state.ownerThreadId.load(std::memory_order_acquire);
            }
            if (owner == current) {
                return true;
            }
            state.ownerThreadViolations.fetch_add(
                1u,
                std::memory_order_relaxed);
            return false;
        }

        void ResetUploadObjects(ClusterGeometryResourceSystemState& state) {
            state.uploadCommandList.Reset();
            state.uploadAllocator.Reset();
            state.uploadFence.Reset();
            if (state.uploadFenceEvent != nullptr) {
                CloseHandle(state.uploadFenceEvent);
                state.uploadFenceEvent = nullptr;
            }
            state.uploadFenceValue = 1;
            state.uploadDevice = nullptr;
        }

        bool EnsureUploadObjects(ClusterGeometryResourceSystemState& state) {
            ID3D12Device* device = state.context.device;
            if (device == nullptr || state.context.queue == nullptr) {
                return false;
            }
            if (state.uploadDevice == device &&
                state.uploadAllocator &&
                state.uploadCommandList &&
                state.uploadFence &&
                state.uploadFenceEvent != nullptr) {
                return true;
            }

            ResetUploadObjects(state);

            HRESULT hr = device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(state.uploadAllocator.GetAddressOf()));
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Create upload allocator");
                return false;
            }

            hr = device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                state.uploadAllocator.Get(),
                nullptr,
                IID_PPV_ARGS(state.uploadCommandList.GetAddressOf()));
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Create upload command list");
                ResetUploadObjects(state);
                return false;
            }
            state.uploadCommandList->Close();

            hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(state.uploadFence.GetAddressOf()));
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Create upload fence");
                ResetUploadObjects(state);
                return false;
            }

            state.uploadFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (state.uploadFenceEvent == nullptr) {
                ResetUploadObjects(state);
                return false;
            }

            state.uploadFenceValue = 1;
            state.uploadDevice = device;
            return true;
        }

        bool CreateClusterGeometryUploadBuffer(
            ClusterGeometryResourceSystemState& state,
            const std::vector<uint8_t>& bytes,
            const wchar_t* gpuName,
            const wchar_t* uploadName,
            ClusterGeometryUploadBuffer& out) {

            out = {};
            ID3D12Device* device = state.context.device;
            if (device == nullptr || bytes.empty()) {
                return false;
            }

            out.byteSize = static_cast<UINT64>(bytes.size());
            const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(out.byteSize);
            const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
            ++state.stats.gpuResourceCreateCount;
            HRESULT hr = device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(out.gpu.GetAddressOf()));
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Create default GPU buffer");
                return false;
            }
            GFX::SetD3D12Name(out.gpu.Get(), gpuName);

            const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
            ++state.stats.gpuResourceCreateCount;
            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(out.upload.GetAddressOf()));
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Create upload staging buffer");
                out = {};
                return false;
            }
            GFX::SetD3D12Name(out.upload.Get(), uploadName);

            void* mapped = nullptr;
            hr = out.upload->Map(0, nullptr, &mapped);
            if (FAILED(hr) || mapped == nullptr) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Map upload staging buffer");
                out = {};
                return false;
            }
            std::memcpy(mapped, bytes.data(), bytes.size());
            out.upload->Unmap(0, nullptr);
            return true;
        }

        bool ExecuteClusterGeometryUpload(
            ClusterGeometryResourceSystemState& state,
            ClusterGeometryUploadBuffer& geometry,
            ClusterGeometryUploadBuffer& metadata) {

            if (!EnsureUploadObjects(state)) {
                return false;
            }

            HRESULT hr = state.uploadAllocator->Reset();
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Reset upload allocator");
                return false;
            }

            hr = state.uploadCommandList->Reset(state.uploadAllocator.Get(), nullptr);
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Reset upload command list");
                return false;
            }

            state.uploadCommandList->CopyBufferRegion(
                geometry.gpu.Get(),
                0,
                geometry.upload.Get(),
                0,
                geometry.byteSize);
            state.uploadCommandList->CopyBufferRegion(
                metadata.gpu.Get(),
                0,
                metadata.upload.Get(),
                0,
                metadata.byteSize);

            D3D12_RESOURCE_BARRIER barriers[] = {
                CD3DX12_RESOURCE_BARRIER::Transition(
                    geometry.gpu.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_GENERIC_READ),
                CD3DX12_RESOURCE_BARRIER::Transition(
                    metadata.gpu.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_GENERIC_READ)
            };
            state.uploadCommandList->ResourceBarrier(2, barriers);

            hr = state.uploadCommandList->Close();
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Close upload command list");
                return false;
            }

            ID3D12CommandList* lists[] = { state.uploadCommandList.Get() };
            state.context.queue->ExecuteCommandLists(1, lists);
            ++state.stats.uploadSubmitCount;

            const uint64_t signalValue = state.uploadFenceValue++;
            hr = state.context.queue->Signal(state.uploadFence.Get(), signalValue);
            if (FAILED(hr)) {
                HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Signal upload fence");
                return false;
            }
            if (state.uploadFence->GetCompletedValue() < signalValue) {
                hr = state.uploadFence->SetEventOnCompletion(signalValue, state.uploadFenceEvent);
                if (FAILED(hr)) {
                    HIKARI_DX_CHECK(hr, "ClusterGeometryResourceSystem::Set upload fence event");
                    return false;
                }
                ++state.stats.fenceWaitCount;
                WaitForSingleObject(state.uploadFenceEvent, INFINITE);
            }

            geometry.upload.Reset();
            metadata.upload.Reset();
            return true;
        }

        RenderResourceDesc BuildResourceDesc(
            const std::string& sourceKey,
            const std::filesystem::path& hcmeshPath,
            const CLUSTER::ClusterGeometryPackedBytes& packed) {

            RenderResourceDesc desc{};
            desc.kind = RenderResourceKind::ClusterGeometry;
            desc.usage =
                RenderResourceUsageFlags::ShaderResource |
                RenderResourceUsageFlags::CopyDest |
                RenderResourceUsageFlags::IndirectArgument;
            desc.lifetime = RenderResourceLifetime::ImportedAsset;
            desc.sourceKey = sourceKey;
            desc.debugName = "ClusterGeometry " + hcmeshPath.filename().generic_string();
            desc.byteSize = packed.layout.byteSize;
            desc.width = packed.layout.byteSize;
            desc.height = 1;
            desc.depthOrArraySize = 1;
            desc.mipLevels = 1;
            return desc;
        }

        bool CreateClusterGeometrySrv(
            ID3D12Device* device,
            const RenderResourceView& view,
            ID3D12Resource* resource,
            uint64_t byteSize) {

            if (device == nullptr ||
                !view.IsValid() ||
                resource == nullptr ||
                byteSize == 0 ||
                byteSize % sizeof(uint32_t) != 0) {
                return false;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements =
                static_cast<UINT>(byteSize / sizeof(uint32_t));
            srvDesc.Buffer.StructureByteStride = 0;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            device->CreateShaderResourceView(resource, &srvDesc, view.cpu);
            return true;
        }

        void ReportDescriptorAllocationFailure(
            const std::string& path,
            const RenderResourceDescriptorPoolStats& descriptorStats,
            const char* reason) {

            const std::string message =
                "[ClusterGeometryResourceSystem][DescriptorArena][LoadFailed] "
                "path=" + path +
                " owner=" + kClusterGeometryDescriptorOwner +
                " needed=" + std::to_string(kClusterGeometryDescriptorCount) +
                " used=" + std::to_string(descriptorStats.clusterUsed) +
                " free=" + std::to_string(descriptorStats.clusterFree) +
                " capacity=" + std::to_string(descriptorStats.clusterCapacity) +
                " reason=" + (reason != nullptr ? reason : "unknown");
            HIKARI_LOG_WARN(message);
        }

        void ReleaseClusterGeometryRecordViews(const ClusterGeometryResourceRecord& record) {
            ClusterGeometryResourceSystemState& state = State();
            RenderResourceView srv = record.srv;
            RenderResourceView metadataSrv = record.metadataSrv;
            RetireRenderResourceDescriptor(
                srv,
                state.context,
                "Cluster Geometry SRV");
            RetireRenderResourceDescriptor(
                metadataSrv,
                state.context,
                "Cluster Geometry Metadata SRV");
            if (record.metadataBuffer &&
                state.context.deferredReleaseQueue != nullptr &&
                state.context.currentFrameRetireFenceValue != 0u) {
                Microsoft::WRL::ComPtr<ID3D12Resource> keepAlive = record.metadataBuffer;
                try {
                    state.context.deferredReleaseQueue->Enqueue(
                        state.context.currentFrameRetireFenceValue,
                        [keepAlive]() mutable {
                            keepAlive.Reset();
                        },
                        "Cluster Geometry Metadata Buffer");
                }
                catch (...) {
                    keepAlive.Reset();
                }
            }
        }

        void RetireClusterGeometryRecordGpuState(
            const ClusterGeometryResourceRecord& record,
            Microsoft::WRL::ComPtr<ID3D12Resource> geometryBuffer,
            const char* debugName) noexcept {

            RenderResourceView srv = record.srv;
            RenderResourceView metadataSrv = record.metadataSrv;
            Microsoft::WRL::ComPtr<ID3D12Resource> metadataBuffer =
                record.metadataBuffer;
            ClusterGeometryResourceSystemState& state = State();
            if (state.context.deferredReleaseQueue != nullptr &&
                state.context.currentFrameRetireFenceValue != 0) {
                const char* descriptorDebugName =
                    debugName != nullptr ? debugName : "Retired Cluster Geometry";
                RetireRenderResourceDescriptor(
                    srv,
                    state.context,
                    descriptorDebugName);
                RetireRenderResourceDescriptor(
                    metadataSrv,
                    state.context,
                    descriptorDebugName);
                try {
                    state.context.deferredReleaseQueue->Enqueue(
                        state.context.currentFrameRetireFenceValue,
                        [geometryBuffer = std::move(geometryBuffer),
                         metadataBuffer = std::move(metadataBuffer)]() mutable {
                            geometryBuffer.Reset();
                            metadataBuffer.Reset();
                        },
                        debugName != nullptr
                            ? debugName
                            : "Retired Cluster Geometry");
                }
                catch (...) {
                    // Descriptor retirement already has a synchronous
                    // fallback. Release the captured resources as well when
                    // queue admission cannot allocate its bookkeeping node.
                    geometryBuffer.Reset();
                    metadataBuffer.Reset();
                }
                return;
            }

            if (srv.IsValid()) {
                ReleaseRenderResourceDescriptor(srv);
            }
            if (metadataSrv.IsValid()) {
                ReleaseRenderResourceDescriptor(metadataSrv);
            }
        }

        ClusterGeometryResourceHandle FindOrRegisterHandle(
            const std::string& sourceKey,
            const std::filesystem::path& hcmeshPath) {

            RenderResourcePool& pool = GetRenderResourcePool();
            if (const RenderResourceHandle cached =
                pool.FindBySourceKey(RenderResourceKind::ClusterGeometry, sourceKey)) {
                return ClusterGeometryResourceHandle::FromUntyped(cached);
            }

            return RegisterVirtualClusterGeometryResource(
                sourceKey,
                "ClusterGeometry " + hcmeshPath.filename().generic_string());
        }

        void RebuildStats() {
            ClusterGeometryResourceSystemState& state = State();
            const uint32_t requests = state.stats.requestCount;
            const uint32_t hits = state.stats.hitCount;
            const uint32_t misses = state.stats.missCount;
            const uint32_t staleReloads = state.stats.staleReloadCount;
            const uint32_t loaded = state.stats.loadedCount;
            const uint32_t failed = state.stats.failedCount;
            const uint32_t missingDevice = state.stats.missingDeviceCount;
            const uint32_t upgraded = state.stats.upgradedVirtualHandleCount;
            const uint32_t descriptorAllocationFailed =
                state.stats.descriptorAllocationFailedCount;
            const uint32_t fileReads = state.stats.fileReadCount;
            const uint32_t gpuResourceCreates =
                state.stats.gpuResourceCreateCount;
            const uint32_t uploadSubmits = state.stats.uploadSubmitCount;
            const uint32_t fenceWaits = state.stats.fenceWaitCount;
            const uint32_t descriptorAllocations =
                state.stats.descriptorAllocationCount;
            const RenderResourceDescriptorPoolStats descriptorPoolStats =
                GetRenderResourceDescriptorPoolStats();
            const uint32_t ownerThreadViolations =
                state.ownerThreadViolations.load(
                    std::memory_order_relaxed);

            ClusterGeometryResourceSystemStats stats{};
            stats.initialized =
                state.context.device != nullptr &&
                state.context.srvHeap != nullptr &&
                descriptorPoolStats.initialized;
            stats.requestCount = requests;
            stats.hitCount = hits;
            stats.missCount = misses;
            stats.staleReloadCount = staleReloads;
            stats.loadedCount = loaded;
            stats.failedCount = failed;
            stats.missingDeviceCount = missingDevice;
            stats.upgradedVirtualHandleCount = upgraded;
            stats.descriptorAllocationFailedCount = descriptorAllocationFailed;
            stats.fileReadCount = fileReads;
            stats.gpuResourceCreateCount = gpuResourceCreates;
            stats.uploadSubmitCount = uploadSubmits;
            stats.fenceWaitCount = fenceWaits;
            stats.descriptorAllocationCount = descriptorAllocations;
            stats.descriptorArenaBegin = descriptorPoolStats.clusterBegin;
            stats.descriptorArenaCapacity = descriptorPoolStats.clusterCapacity;
            stats.descriptorArenaUsed = descriptorPoolStats.clusterUsed;
            stats.descriptorArenaFree = descriptorPoolStats.clusterFree;
            stats.ownerThreadViolationCount = ownerThreadViolations;
            stats.resourceCount = NUMERIC::SaturateToUint32(state.recordsBySourceKey.size());

            for (const auto& pair : state.recordsBySourceKey) {
                const ClusterGeometryResourceRecord& record = pair.second;
                if (!record.ready) {
                    continue;
                }
                ++stats.readyResourceCount;
                if (record.srv.IsValid() && record.metadataSrv.IsValid()) {
                    ++stats.shaderVisibleResourceCount;
                }
                else {
                    ++stats.missingDescriptorCount;
                }
                stats.surfaceCount += record.layout.surfaceCount;
                stats.surfaceLodRangeCount += record.layout.surfaceLodRangeCount;
                stats.surfaceSectionCount += record.layout.surfaceSectionCount;
                stats.clusterCount += record.layout.clusterCount;
                stats.pageCount += record.layout.pageCount;
                stats.vertexCount += record.layout.vertexCount;
                stats.indexCount += record.layout.indexCount;
                stats.meshletPrimitiveCount += record.layout.meshletPrimitiveCount;
                stats.surfaceRangeCount += NUMERIC::SaturateToUint32(record.surfaceRanges.size());
                stats.gpuBufferBytes += record.layout.byteSize + record.metadataBufferBytes;
            }

            state.stats = stats;
        }
    } // namespace

    void UpdateClusterGeometryResourceContext(const GFX::Context& ctx) {
        ClusterGeometryResourceSystemState& state = State();
        if (!EnterOwnerThread(state)) {
            HIKARI_LOG_WARN(
                "[ClusterGeometryResourceSystem] context update rejected "
                "outside the owner thread.");
            return;
        }
        if (!IsContextCompatible(state.context, ctx)) {
            if (!state.contextMismatchReported) {
                HIKARI_LOG_WARN(
                    "[ClusterGeometryResourceSystem] context update rejected "
                    "for an incompatible device or SRV heap.");
                state.contextMismatchReported = true;
            }
            RebuildStats();
            return;
        }
        state.contextMismatchReported = false;
        state.context = ctx;
        RebuildStats();
    }

    void ShutdownClusterGeometryResourceSystem() {
        ClusterGeometryResourceSystemState& state = State();
        if (!EnterOwnerThread(state)) {
            HIKARI_LOG_WARN(
                "[ClusterGeometryResourceSystem] shutdown rejected outside "
                "the owner thread.");
            return;
        }
        state.memorySourcesBySourceKey.clear();
        for (const auto& pair : state.recordsBySourceKey) {
            const ClusterGeometryResourceRecord& record = pair.second;
            ReleaseClusterGeometryRecordViews(record);
            if (record.handle) {
                GetRenderResourcePool().Release(record.handle.ToUntyped());
            }
        }
        state.recordsBySourceKey.clear();
        state.sourceKeyByHandle.clear();
        state.context = {};
        state.contextMismatchReported = false;
        state.stats = {};
        ResetUploadObjects(state);
        state.nextMemorySourceRevision = 1;
        state.ownerThreadViolations.store(0u, std::memory_order_relaxed);
        state.ownerThreadId.store(0u, std::memory_order_release);
    }

    void RegisterClusterGeometryPackedSource(
        const std::string& path,
        CLUSTER::ClusterGeometryPackedBytes packed) {

        ClusterGeometryResourceSystemState& state = State();
        if (!EnterOwnerThread(state)) {
            HIKARI_LOG_WARN(
                "[ClusterGeometryResourceSystem] packed source registration "
                "rejected outside the owner thread.");
            return;
        }
        const std::string sourceKey =
            BuildClusterGeometryResourceSourceKey(path);
        if (sourceKey.empty()) {
            return;
        }
        const auto existingSource =
            state.memorySourcesBySourceKey.find(sourceKey);
        if (existingSource != state.memorySourcesBySourceKey.end() &&
            INTERNAL::ClusterGeometryPackedContentEqual(
                existingSource->second.packed,
                packed)) {
            return;
        }
        ClusterGeometryMemorySource source{};
        source.packed = std::move(packed);
        source.revision = NextMemorySourceRevision(state);
        state.memorySourcesBySourceKey[sourceKey] = std::move(source);

        const auto record = state.recordsBySourceKey.find(sourceKey);
        if (record != state.recordsBySourceKey.end() &&
            record->second.memoryResident) {
            record->second.ready = false;
        }
    }

    void UnregisterClusterGeometryPackedSource(
        const std::string& path) {

        ClusterGeometryResourceSystemState& state = State();
        if (!EnterOwnerThread(state)) {
            HIKARI_LOG_WARN(
                "[ClusterGeometryResourceSystem] packed source removal "
                "rejected outside the owner thread.");
            return;
        }
        const std::string sourceKey =
            BuildClusterGeometryResourceSourceKey(path);
        if (sourceKey.empty()) {
            return;
        }
        const auto record = state.recordsBySourceKey.find(sourceKey);
        if (record != state.recordsBySourceKey.end() &&
            record->second.memoryResident) {
            ClusterGeometryResourceRecord& resource = record->second;
            Microsoft::WRL::ComPtr<ID3D12Resource> geometryBuffer{};
            if (resource.handle) {
                geometryBuffer =
                    GetRenderResourcePool().GetResource(
                        resource.handle.ToUntyped());
                if (!GetRenderResourcePool().Release(
                        resource.handle.ToUntyped())) {
                    HIKARI_LOG_WARN(
                        "[ClusterGeometryResourceSystem][Unregister] "
                        "failed to release resource handle for path=" +
                        resource.sourcePath.generic_string());
                    return;
                }
            }

            RetireClusterGeometryRecordGpuState(
                resource,
                std::move(geometryBuffer),
                "Unregistered Cluster Geometry");
            state.sourceKeyByHandle.erase(PackHandle(resource.handle));
            state.recordsBySourceKey.erase(record);
        }
        state.memorySourcesBySourceKey.erase(sourceKey);
        RebuildStats();
    }

    ClusterGeometryResourceHandle LoadClusterGeometryResource(
        const std::filesystem::path& hcmeshPath) {

        ClusterGeometryResourceSystemState& state = State();
        if (!EnterOwnerThread(state)) {
            return {};
        }
        ++state.stats.requestCount;
        const std::string normalizedPath =
            INTERNAL::NormalizeClusterGeometryPath(hcmeshPath);
        const std::string sourceKey =
            INTERNAL::BuildClusterGeometrySourceKeyFromNormalizedPath(
                normalizedPath);
        if (sourceKey.empty() || normalizedPath.empty()) {
            ++state.stats.failedCount;
            return {};
        }

        const auto memorySource =
            state.memorySourcesBySourceKey.find(sourceKey);
        const bool fromMemory =
            memorySource != state.memorySourcesBySourceKey.end();
        const uint64_t memoryRevision = fromMemory
            ? memorySource->second.revision
            : 0;
        INTERNAL::ClusterGeometryFileStamp fileStamp{};
        bool fileStampRead = false;
        // File-backed HCMESH still lacks an AssetRegistry artifact generation.
        // Coalesce the development fallback stamp check per presentation fence;
        // cooked identity must eventually replace mtime/size polling.
        const uint64_t validationFenceValue =
            state.context.currentFrameRetireFenceValue != 0u
                ? state.context.currentFrameRetireFenceValue
                : (std::numeric_limits<uint64_t>::max)();

        const auto cached = state.recordsBySourceKey.find(sourceKey);
        if (cached != state.recordsBySourceKey.end()) {
            const bool alive =
                cached->second.ready &&
                GetRenderResourcePool().IsAlive(
                    cached->second.handle.ToUntyped());
            const bool samePath =
                cached->second.sourcePath.generic_string() ==
                    normalizedPath;
            const bool memoryHit =
                alive &&
                samePath &&
                fromMemory &&
                cached->second.memoryResident &&
                cached->second.sourceRevision == memoryRevision;
            if (memoryHit) {
                ++state.stats.hitCount;
                return cached->second.handle;
            }

            const bool fileValidationCurrent =
                alive &&
                samePath &&
                !fromMemory &&
                !cached->second.memoryResident &&
                validationFenceValue != 0u &&
                cached->second.sourceValidationFenceValue ==
                    validationFenceValue;
            if (fileValidationCurrent) {
                ++state.stats.hitCount;
                return cached->second.handle;
            }
            if (!fromMemory) {
                fileStamp =
                    INTERNAL::ReadClusterGeometryFileStamp(normalizedPath);
                fileStampRead = true;
            }
            const bool fileHit =
                alive &&
                samePath &&
                !fromMemory &&
                !cached->second.memoryResident &&
                fileStamp.available &&
                cached->second.sourceWriteTime == fileStamp.writeTime &&
                cached->second.sourceFileSize == fileStamp.fileSize;
            if (fileHit) {
                cached->second.sourceValidationFenceValue =
                    validationFenceValue;
                ++state.stats.hitCount;
                return cached->second.handle;
            }
            ++state.stats.staleReloadCount;
        }

        if (!fromMemory && !fileStampRead) {
            fileStamp =
                INTERNAL::ReadClusterGeometryFileStamp(normalizedPath);
            fileStampRead = true;
        }

        ++state.stats.missCount;
        if (state.context.device == nullptr || state.context.queue == nullptr) {
            ++state.stats.failedCount;
            ++state.stats.missingDeviceCount;
            return {};
        }

        const RenderResourceDescriptorPoolStats descriptorStats =
            GetRenderResourceDescriptorPoolStats();
        if (!descriptorStats.initialized ||
            descriptorStats.clusterFree < kClusterGeometryDescriptorCount) {
            ReportDescriptorAllocationFailure(
                normalizedPath,
                descriptorStats,
                descriptorStats.initialized
                    ? "cluster-arena-exhausted"
                    : "cluster-arena-unavailable");
            ++state.stats.failedCount;
            ++state.stats.descriptorAllocationFailedCount;
            return {};
        }

        CLUSTER::ClusterGeometryPackedBytes packed{};
        if (fromMemory) {
            packed = memorySource->second.packed;
        } else {
            std::string readMessage{};
            ++state.stats.fileReadCount;
            if (!ASSETS::GEOMETRY::ReadHcmeshPackedFile(
                    normalizedPath,
                    packed,
                    readMessage)) {
                HIKARI_LOG_WARN(
                    "[ClusterGeometryResourceSystem][Load] " +
                    (readMessage.empty()
                        ? normalizedPath
                        : readMessage));
                ++state.stats.failedCount;
                return {};
            }
        }

        const std::vector<RenderResourceView> descriptorRange =
            AllocateClusterGeometryDescriptorRange(
                kClusterGeometryDescriptorCount);
        if (descriptorRange.size() != kClusterGeometryDescriptorCount ||
            !descriptorRange[0].IsValid() ||
            !descriptorRange[1].IsValid() ||
            descriptorRange[0].descriptorIndex + 1u !=
                descriptorRange[1].descriptorIndex) {
            ReportDescriptorAllocationFailure(
                normalizedPath,
                GetRenderResourceDescriptorPoolStats(),
                "cluster-arena-allocation-failed");
            for (const RenderResourceView& descriptor : descriptorRange) {
                ReleaseRenderResourceDescriptor(descriptor);
            }
            ++state.stats.failedCount;
            ++state.stats.descriptorAllocationFailedCount;
            return {};
        }
        RenderResourceView srv = descriptorRange[0];
        RenderResourceView metadataSrv = descriptorRange[1];
        state.stats.descriptorAllocationCount +=
            (srv.IsValid() ? 1u : 0u) +
            (metadataSrv.IsValid() ? 1u : 0u);

        ClusterGeometryUploadBuffer geometryUpload{};
        ClusterGeometryUploadBuffer metadataUpload{};
        if (!CreateClusterGeometryUploadBuffer(
                state,
                packed.geometryBytes,
                L"Cluster Geometry GPU Buffer",
                L"Cluster Geometry Upload Staging",
                geometryUpload) ||
            !CreateClusterGeometryUploadBuffer(
                state,
                packed.metadataBytes,
                L"Cluster Geometry Metadata GPU Buffer",
                L"Cluster Geometry Metadata Upload Staging",
                metadataUpload) ||
            !ExecuteClusterGeometryUpload(state, geometryUpload, metadataUpload)) {
            ReleaseRenderResourceDescriptor(srv);
            ReleaseRenderResourceDescriptor(metadataSrv);
            ++state.stats.failedCount;
            return {};
        }

        ClusterGeometryResourceHandle handle =
            FindOrRegisterHandle(sourceKey, normalizedPath);
        if (!handle) {
            ReleaseRenderResourceDescriptor(srv);
            ReleaseRenderResourceDescriptor(metadataSrv);
            ++state.stats.failedCount;
            return {};
        }

        const auto oldRecordIt = state.recordsBySourceKey.find(sourceKey);
        const bool replacingRecord =
            oldRecordIt != state.recordsBySourceKey.end();
        Microsoft::WRL::ComPtr<ID3D12Resource> replacedGeometryBuffer{};
        if (replacingRecord) {
            if (ID3D12Resource* oldGeometry =
                    GetRenderResourcePool().GetResource(handle.ToUntyped())) {
                replacedGeometryBuffer = oldGeometry;
            }
        }

        if (!CreateClusterGeometrySrv(
                state.context.device,
                srv,
                geometryUpload.gpu.Get(),
                packed.layout.byteSize) ||
            !CreateClusterGeometrySrv(
                state.context.device,
                metadataSrv,
                metadataUpload.gpu.Get(),
                packed.metadataByteSize)) {
            ReportDescriptorAllocationFailure(
                normalizedPath,
                GetRenderResourceDescriptorPoolStats(),
                "cluster-srv-view-create-failed");
            if (srv.IsValid()) {
                ReleaseRenderResourceDescriptor(srv);
            }
            if (metadataSrv.IsValid()) {
                ReleaseRenderResourceDescriptor(metadataSrv);
            }
            ++state.stats.failedCount;
            ++state.stats.descriptorAllocationFailedCount;
            return {};
        }

        if (!GetRenderResourcePool().AttachOwnedResource(
            handle.ToUntyped(),
            std::move(geometryUpload.gpu),
            BuildResourceDesc(sourceKey, normalizedPath, packed))) {
            ReleaseRenderResourceDescriptor(srv);
            ReleaseRenderResourceDescriptor(metadataSrv);
            ++state.stats.failedCount;
            return {};
        }

        if (!GetRenderResourcePool().SetView(handle.ToUntyped(), RenderResourceViewKind::Srv, srv)) {
            ReportDescriptorAllocationFailure(
                normalizedPath,
                GetRenderResourceDescriptorPoolStats(),
                "cluster-resource-view-publish-failed");
            ReleaseRenderResourceDescriptor(srv);
            ReleaseRenderResourceDescriptor(metadataSrv);
            const auto staleRecordIt = state.recordsBySourceKey.find(sourceKey);
            if (staleRecordIt != state.recordsBySourceKey.end()) {
                ReleaseClusterGeometryRecordViews(staleRecordIt->second);
                staleRecordIt->second.srv = {};
                staleRecordIt->second.metadataSrv = {};
                staleRecordIt->second.metadataBuffer.Reset();
                staleRecordIt->second.metadataBufferBytes = 0;
                staleRecordIt->second.ready = false;
            }
            ++state.stats.failedCount;
            ++state.stats.descriptorAllocationFailedCount;
            return {};
        }

        if (replacingRecord) {
            RetireClusterGeometryRecordGpuState(
                oldRecordIt->second,
                std::move(replacedGeometryBuffer),
                "Replaced Cluster Geometry");
            if (oldRecordIt->second.handle != handle) {
                state.sourceKeyByHandle.erase(PackHandle(oldRecordIt->second.handle));
            }
        }

        ClusterGeometryResourceRecord record{};
        record.handle = handle;
        record.sourceKey = sourceKey;
        record.sourcePath = normalizedPath;
        record.sourceWriteTime = fromMemory
            ? std::filesystem::file_time_type{}
            : fileStamp.writeTime;
        record.sourceRevision = memoryRevision;
        record.sourceFileSize = fromMemory ? 0 : fileStamp.fileSize;
        record.sourceValidationFenceValue = fromMemory
            ? 0u
            : validationFenceValue;
        record.layout = packed.layout;
        record.surfaceRanges = packed.surfaceRanges;
        record.surfaceLodRanges = packed.surfaceLodRanges;
        record.surfaceSections = packed.surfaceSections;
        record.srv = srv;
        record.metadataSrv = metadataSrv;
        record.metadataBuffer = std::move(metadataUpload.gpu);
        record.metadataBufferBytes = packed.metadataByteSize;
        record.ready = true;
        record.memoryResident = fromMemory;
        state.recordsBySourceKey[sourceKey] = std::move(record);
        state.sourceKeyByHandle[PackHandle(handle)] = sourceKey;

        ++state.stats.loadedCount;
        if (!replacingRecord) {
            ++state.stats.upgradedVirtualHandleCount;
        }
        if (fromMemory) {
            HIKARI_LOG_INFO(
                "[ClusterGeometry][Load][Memory] sourceKey=" +
                sourceKey);
        }
        RebuildStats();
        return handle;
    }

    const ClusterGeometryResourceRecord* GetClusterGeometryResource(
        ClusterGeometryResourceHandle handle) {

        if (!handle) {
            return nullptr;
        }

        ClusterGeometryResourceSystemState& state = State();
        if (!EnterOwnerThread(state)) {
            return nullptr;
        }
        const auto keyIt = state.sourceKeyByHandle.find(PackHandle(handle));
        if (keyIt == state.sourceKeyByHandle.end()) {
            return nullptr;
        }
        const auto recordIt = state.recordsBySourceKey.find(keyIt->second);
        return recordIt != state.recordsBySourceKey.end() ? &recordIt->second : nullptr;
    }

    const CLUSTER::ClusterGeometrySurfaceRange* FindClusterGeometrySurfaceRange(
        ClusterGeometryResourceHandle handle,
        uint32_t nodeIndex,
        uint32_t meshIndex,
        uint32_t primitiveIndex) {

        const ClusterGeometryResourceRecord* record = GetClusterGeometryResource(handle);
        if (record == nullptr || !record->ready) {
            return nullptr;
        }

        for (const CLUSTER::ClusterGeometrySurfaceRange& range : record->surfaceRanges) {
            if (range.nodeIndex == nodeIndex &&
                range.meshIndex == meshIndex &&
                range.primitiveIndex == primitiveIndex) {
                return &range;
            }
        }

        const CLUSTER::ClusterGeometrySurfaceRange* uniqueMeshPrimitiveRange = nullptr;
        for (const CLUSTER::ClusterGeometrySurfaceRange& range : record->surfaceRanges) {
            if (range.meshIndex != meshIndex || range.primitiveIndex != primitiveIndex) {
                continue;
            }
            if (uniqueMeshPrimitiveRange != nullptr) {
                return nullptr;
            }
            uniqueMeshPrimitiveRange = &range;
        }
        if (uniqueMeshPrimitiveRange != nullptr) {
            // node ID が一致しない旧 surface でも、mesh/primitive が一意なら同じ geometry range として扱う。
            return uniqueMeshPrimitiveRange;
        }

        // HCMESH は node transform を頂点へ bake しているため、node が違う surface は別ジオメトリとして扱う。
        return nullptr;
    }

    const CLUSTER::ClusterGeometrySurfaceLodRange* FindClusterGeometrySurfaceLodRange(
        ClusterGeometryResourceHandle handle,
        uint32_t firstLodRange,
        uint32_t lodRangeCount,
        uint32_t lodIndex) {

        const ClusterGeometryResourceRecord* record = GetClusterGeometryResource(handle);
        if (record == nullptr ||
            !record->ready ||
            lodRangeCount == 0u ||
            firstLodRange >= record->surfaceLodRanges.size()) {
            return nullptr;
        }

        const uint64_t begin = firstLodRange;
        const uint64_t end = (std::min)(
            begin + static_cast<uint64_t>(lodRangeCount),
            static_cast<uint64_t>(record->surfaceLodRanges.size()));
        for (uint64_t i = begin; i < end; ++i) {
            const CLUSTER::ClusterGeometrySurfaceLodRange& range =
                record->surfaceLodRanges[static_cast<size_t>(i)];
            if (range.lodIndex == lodIndex) {
                return &range;
            }
        }
        return nullptr;
    }

    ClusterGeometryResourceSystemStats GetClusterGeometryResourceSystemStats() {
        if (!EnterOwnerThread(State())) {
            return {};
        }
        RebuildStats();
        return State().stats;
    }

} // namespace HIKARI::RENDER3D
