#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"

#include <algorithm>
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
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_RenderResourceSystem.h"

namespace HIKARI::RENDER3D {

    namespace {
        struct ClusterGeometryResourceSystemState {
            GFX::Context context{};
            std::unordered_map<std::string, ClusterGeometryResourceRecord> recordsBySourceKey{};
            std::unordered_map<uint64_t, std::string> sourceKeyByHandle{};
            ClusterGeometryResourceSystemStats stats{};
            ID3D12Device* uploadDevice = nullptr;
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> uploadAllocator{};
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uploadCommandList{};
            Microsoft::WRL::ComPtr<ID3D12Fence> uploadFence{};
            HANDLE uploadFenceEvent = nullptr;
            uint64_t uploadFenceValue = 1;
        };

        ClusterGeometryResourceSystemState& State() {
            static ClusterGeometryResourceSystemState state{};
            return state;
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

        std::filesystem::file_time_type ReadSourceWriteTime(const std::filesystem::path& path) {
            std::error_code ec{};
            const std::filesystem::file_time_type value = std::filesystem::last_write_time(path, ec);
            return ec ? std::filesystem::file_time_type{} : value;
        }

        bool IsSameSourcePath(
            const std::filesystem::path& lhs,
            const std::filesystem::path& rhs) {

            return lhs.lexically_normal().generic_string() ==
                rhs.lexically_normal().generic_string();
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
            ID3D12Device* device,
            const std::vector<uint8_t>& bytes,
            const wchar_t* gpuName,
            const wchar_t* uploadName,
            ClusterGeometryUploadBuffer& out) {

            out = {};
            if (device == nullptr || bytes.empty()) {
                return false;
            }

            out.byteSize = static_cast<UINT64>(bytes.size());
            const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(out.byteSize);
            const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
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

        RenderResourceView CreateClusterGeometrySrv(
            ID3D12Resource* resource,
            uint64_t byteSize) {

            if (resource == nullptr || byteSize == 0) {
                return {};
            }

            return AllocateBufferSrvDescriptor(
                resource,
                DXGI_FORMAT_R32_TYPELESS,
                static_cast<UINT>(byteSize / sizeof(uint32_t)),
                0,
                D3D12_BUFFER_SRV_FLAG_RAW);
        }

        void ReleaseClusterGeometryRecordViews(const ClusterGeometryResourceRecord& record) {
            if (record.srv.IsValid()) {
                ReleaseRenderResourceDescriptor(record.srv);
            }
            if (record.metadataSrv.IsValid()) {
                ReleaseRenderResourceDescriptor(record.metadataSrv);
            }
            if (record.metadataBuffer && State().context.deferredReleaseQueue != nullptr) {
                Microsoft::WRL::ComPtr<ID3D12Resource> keepAlive = record.metadataBuffer;
                State().context.deferredReleaseQueue->Enqueue(
                    State().context.currentFrameRetireFenceValue,
                    [keepAlive]() mutable {
                        keepAlive.Reset();
                    },
                    "Cluster Geometry Metadata Buffer");
            }
        }

        void RetireReplacedClusterGeometryRecord(
            const ClusterGeometryResourceRecord& record,
            Microsoft::WRL::ComPtr<ID3D12Resource> geometryBuffer) {

            const RenderResourceView srv = record.srv;
            const RenderResourceView metadataSrv = record.metadataSrv;
            Microsoft::WRL::ComPtr<ID3D12Resource> metadataBuffer =
                record.metadataBuffer;
            ClusterGeometryResourceSystemState& state = State();
            if (state.context.deferredReleaseQueue != nullptr &&
                state.context.currentFrameRetireFenceValue != 0) {
                state.context.deferredReleaseQueue->Enqueue(
                    state.context.currentFrameRetireFenceValue,
                    [srv,
                     metadataSrv,
                     geometryBuffer = std::move(geometryBuffer),
                     metadataBuffer = std::move(metadataBuffer)]() mutable {
                        if (srv.IsValid()) {
                            ReleaseRenderResourceDescriptor(srv);
                        }
                        if (metadataSrv.IsValid()) {
                            ReleaseRenderResourceDescriptor(metadataSrv);
                        }
                        geometryBuffer.Reset();
                        metadataBuffer.Reset();
                    },
                    "Replaced Cluster Geometry");
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

            ClusterGeometryResourceSystemStats stats{};
            stats.initialized = state.context.device != nullptr && state.context.srvHeap != nullptr;
            stats.requestCount = requests;
            stats.hitCount = hits;
            stats.missCount = misses;
            stats.staleReloadCount = staleReloads;
            stats.loadedCount = loaded;
            stats.failedCount = failed;
            stats.missingDeviceCount = missingDevice;
            stats.upgradedVirtualHandleCount = upgraded;
            stats.descriptorAllocationFailedCount = descriptorAllocationFailed;
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
        State().context = ctx;
        RebuildStats();
    }

    void ShutdownClusterGeometryResourceSystem() {
        ClusterGeometryResourceSystemState& state = State();
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
        state.stats = {};
        ResetUploadObjects(state);
    }

    ClusterGeometryResourceHandle LoadClusterGeometryResource(
        const std::string& sourceKey,
        const std::filesystem::path& hcmeshPath) {

        ClusterGeometryResourceSystemState& state = State();
        ++state.stats.requestCount;

        if (sourceKey.empty() || hcmeshPath.empty()) {
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        const auto cached = state.recordsBySourceKey.find(sourceKey);
        if (cached != state.recordsBySourceKey.end() &&
            cached->second.ready &&
            GetRenderResourcePool().IsAlive(cached->second.handle.ToUntyped())) {
            const std::filesystem::file_time_type currentWriteTime = ReadSourceWriteTime(hcmeshPath);
            if (IsSameSourcePath(cached->second.sourcePath, hcmeshPath) &&
                cached->second.sourceWriteTime == currentWriteTime) {
                ++state.stats.hitCount;
                RebuildStats();
                return cached->second.handle;
            }
            ++state.stats.staleReloadCount;
        }

        ++state.stats.missCount;
        if (state.context.device == nullptr || state.context.queue == nullptr) {
            ++state.stats.failedCount;
            ++state.stats.missingDeviceCount;
            RebuildStats();
            return {};
        }

        CLUSTER::ClusterGeometryPackedBytes packed{};
        std::string readMessage{};
        if (!ASSETS::GEOMETRY::ReadHcmeshPackedFile(hcmeshPath, packed, readMessage)) {
            HIKARI_LOG_WARN(
                "[ClusterGeometryResourceSystem][Load] " +
                (readMessage.empty() ? hcmeshPath.generic_string() : readMessage));
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        ClusterGeometryUploadBuffer geometryUpload{};
        ClusterGeometryUploadBuffer metadataUpload{};
        if (!CreateClusterGeometryUploadBuffer(
                state.context.device,
                packed.geometryBytes,
                L"Cluster Geometry GPU Buffer",
                L"Cluster Geometry Upload Staging",
                geometryUpload) ||
            !CreateClusterGeometryUploadBuffer(
                state.context.device,
                packed.metadataBytes,
                L"Cluster Geometry Metadata GPU Buffer",
                L"Cluster Geometry Metadata Upload Staging",
                metadataUpload) ||
            !ExecuteClusterGeometryUpload(state, geometryUpload, metadataUpload)) {
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        ClusterGeometryResourceHandle handle = FindOrRegisterHandle(sourceKey, hcmeshPath);
        if (!handle) {
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        const auto oldRecordIt = state.recordsBySourceKey.find(sourceKey);
        Microsoft::WRL::ComPtr<ID3D12Resource> replacedGeometryBuffer{};
        if (oldRecordIt != state.recordsBySourceKey.end()) {
            if (ID3D12Resource* oldGeometry =
                    GetRenderResourcePool().GetResource(handle.ToUntyped())) {
                replacedGeometryBuffer = oldGeometry;
            }
        }

        RenderResourceView srv = CreateClusterGeometrySrv(geometryUpload.gpu.Get(), packed.layout.byteSize);
        RenderResourceView metadataSrv = CreateClusterGeometrySrv(metadataUpload.gpu.Get(), packed.metadataByteSize);
        if (!srv.IsValid() || !metadataSrv.IsValid()) {
            if (srv.IsValid()) {
                ReleaseRenderResourceDescriptor(srv);
            }
            if (metadataSrv.IsValid()) {
                ReleaseRenderResourceDescriptor(metadataSrv);
            }
            ++state.stats.failedCount;
            ++state.stats.descriptorAllocationFailedCount;
            RebuildStats();
            return {};
        }

        if (!GetRenderResourcePool().AttachOwnedResource(
            handle.ToUntyped(),
            std::move(geometryUpload.gpu),
            BuildResourceDesc(sourceKey, hcmeshPath, packed))) {
            ReleaseRenderResourceDescriptor(srv);
            ReleaseRenderResourceDescriptor(metadataSrv);
            ++state.stats.failedCount;
            RebuildStats();
            return {};
        }

        if (!GetRenderResourcePool().SetView(handle.ToUntyped(), RenderResourceViewKind::Srv, srv)) {
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
            RebuildStats();
            return {};
        }

        if (oldRecordIt != state.recordsBySourceKey.end()) {
            RetireReplacedClusterGeometryRecord(
                oldRecordIt->second,
                std::move(replacedGeometryBuffer));
            if (oldRecordIt->second.handle != handle) {
                state.sourceKeyByHandle.erase(PackHandle(oldRecordIt->second.handle));
            }
        }

        ClusterGeometryResourceRecord record{};
        record.handle = handle;
        record.sourceKey = sourceKey;
        record.sourcePath = hcmeshPath;
        record.sourceWriteTime = ReadSourceWriteTime(hcmeshPath);
        record.layout = packed.layout;
        record.surfaceRanges = packed.surfaceRanges;
        record.surfaceLodRanges = packed.surfaceLodRanges;
        record.surfaceSections = packed.surfaceSections;
        record.srv = srv;
        record.metadataSrv = metadataSrv;
        record.metadataBuffer = std::move(metadataUpload.gpu);
        record.metadataBufferBytes = packed.metadataByteSize;
        record.ready = true;
        state.recordsBySourceKey[sourceKey] = std::move(record);
        state.sourceKeyByHandle[PackHandle(handle)] = sourceKey;

        ++state.stats.loadedCount;
        ++state.stats.upgradedVirtualHandleCount;
        RebuildStats();
        return handle;
    }

    const ClusterGeometryResourceRecord* GetClusterGeometryResource(
        ClusterGeometryResourceHandle handle) {

        if (!handle) {
            return nullptr;
        }

        ClusterGeometryResourceSystemState& state = State();
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
        RebuildStats();
        return State().stats;
    }

} // namespace HIKARI::RENDER3D
