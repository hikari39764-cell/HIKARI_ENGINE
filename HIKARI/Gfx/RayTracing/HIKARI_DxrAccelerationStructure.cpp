#include "Gfx/RayTracing/HIKARI_DxrAccelerationStructure.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"

namespace HIKARI::GFX::RAYTRACING {

    namespace {

        constexpr UINT64 kAccelerationStructureAlignment =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;

        UINT64 AlignUp(UINT64 value, UINT64 alignment) {
            return (value + alignment - 1u) &
                ~(alignment - 1u);
        }

        void SetMessage(
            std::string* outMessage,
            std::string message) {
            if (outMessage != nullptr) {
                *outMessage = std::move(message);
            }
        }

        bool CreateBuffer(
            ID3D12Device* device,
            UINT64 size,
            D3D12_HEAP_TYPE heapType,
            D3D12_RESOURCE_FLAGS flags,
            D3D12_RESOURCE_STATES initialState,
            ID3D12Resource** outResource) {

            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = heapType;
            heap.CreationNodeMask = 1u;
            heap.VisibleNodeMask = 1u;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = (std::max)(UINT64{ 1u }, size);
            desc.Height = 1u;
            desc.DepthOrArraySize = 1u;
            desc.MipLevels = 1u;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1u;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = flags;

            return SUCCEEDED(device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                initialState,
                nullptr,
                IID_PPV_ARGS(outResource)));
        }

        template<class T>
        bool CreateUploadBuffer(
            ID3D12Device* device,
            const std::vector<T>& values,
            Microsoft::WRL::ComPtr<ID3D12Resource>& outResource) {

            if (values.empty() ||
                !CreateBuffer(
                    device,
                    sizeof(T) * values.size(),
                    D3D12_HEAP_TYPE_UPLOAD,
                    D3D12_RESOURCE_FLAG_NONE,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    outResource.GetAddressOf())) {
                return false;
            }

            void* mapped = nullptr;
            if (FAILED(outResource->Map(0u, nullptr, &mapped))) {
                outResource.Reset();
                return false;
            }
            std::memcpy(
                mapped,
                values.data(),
                sizeof(T) * values.size());
            outResource->Unmap(0u, nullptr);
            return true;
        }

    } // namespace

    bool DxrTriangleAccelerationStructure::Build(
        const Context& graphics,
        const std::vector<DxrPosition>& positions,
        const std::vector<uint32_t>& indices,
        std::string* outMessage) {

        Reset();
        if (graphics.device == nullptr ||
            graphics.cmdList == nullptr ||
            positions.empty() ||
            indices.empty() ||
            indices.size() % 3u != 0u) {
            SetMessage(
                outMessage,
                "DXR acceleration structure input is invalid.");
            return false;
        }

        const DxrCapabilities capabilities =
            QueryDxrCapabilities(graphics.device);
        if (!capabilities.SupportsInlineRayQuery()) {
            SetMessage(
                outMessage,
                "DXR 1.1 with Shader Model 6.5 is unavailable.");
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device5> device{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4>
            commandList{};
        if (FAILED(graphics.device->QueryInterface(
                IID_PPV_ARGS(device.GetAddressOf()))) ||
            FAILED(graphics.cmdList->QueryInterface(
                IID_PPV_ARGS(commandList.GetAddressOf())))) {
            SetMessage(
                outMessage,
                "Required DXR device or command-list interface is unavailable.");
            return false;
        }

        if (!CreateUploadBuffer(
                graphics.device,
                positions,
                vertexBuffer_) ||
            !CreateUploadBuffer(
                graphics.device,
                indices,
                indexBuffer_)) {
            SetMessage(
                outMessage,
                "Failed to upload DXR triangle geometry.");
            Reset();
            return false;
        }

        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
        geometry.Type =
            D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry.Flags =
            D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.Triangles.VertexBuffer.StartAddress =
            vertexBuffer_->GetGPUVirtualAddress();
        geometry.Triangles.VertexBuffer.StrideInBytes =
            sizeof(DxrPosition);
        geometry.Triangles.VertexCount =
            static_cast<UINT>(positions.size());
        geometry.Triangles.VertexFormat =
            DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.Triangles.IndexBuffer =
            indexBuffer_->GetGPUVirtualAddress();
        geometry.Triangles.IndexCount =
            static_cast<UINT>(indices.size());
        geometry.Triangles.IndexFormat =
            DXGI_FORMAT_R32_UINT;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS
            bottomInputs{};
        bottomInputs.Type =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottomInputs.DescsLayout =
            D3D12_ELEMENTS_LAYOUT_ARRAY;
        bottomInputs.Flags =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        bottomInputs.NumDescs = 1u;
        bottomInputs.pGeometryDescs = &geometry;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO
            bottomInfo{};
        device->GetRaytracingAccelerationStructurePrebuildInfo(
            &bottomInputs,
            &bottomInfo);
        if (bottomInfo.ResultDataMaxSizeInBytes == 0u ||
            !CreateBuffer(
                graphics.device,
                AlignUp(
                    bottomInfo.ScratchDataSizeInBytes,
                    kAccelerationStructureAlignment),
                D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                bottomScratch_.GetAddressOf()) ||
            !CreateBuffer(
                graphics.device,
                AlignUp(
                    bottomInfo.ResultDataMaxSizeInBytes,
                    kAccelerationStructureAlignment),
                D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                bottomLevel_.GetAddressOf())) {
            SetMessage(
                outMessage,
                "Failed to allocate DXR bottom-level acceleration structure.");
            Reset();
            return false;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC
            bottomBuild{};
        bottomBuild.Inputs = bottomInputs;
        bottomBuild.ScratchAccelerationStructureData =
            bottomScratch_->GetGPUVirtualAddress();
        bottomBuild.DestAccelerationStructureData =
            bottomLevel_->GetGPUVirtualAddress();
        commandList->BuildRaytracingAccelerationStructure(
            &bottomBuild,
            0u,
            nullptr);

        D3D12_RESOURCE_BARRIER bottomBarrier{};
        bottomBarrier.Type =
            D3D12_RESOURCE_BARRIER_TYPE_UAV;
        bottomBarrier.UAV.pResource = bottomLevel_.Get();
        commandList->ResourceBarrier(1u, &bottomBarrier);

        std::vector<D3D12_RAYTRACING_INSTANCE_DESC>
            instances(1u);
        D3D12_RAYTRACING_INSTANCE_DESC& instance =
            instances.front();
        instance.Transform[0][0] = 1.0f;
        instance.Transform[1][1] = 1.0f;
        instance.Transform[2][2] = 1.0f;
        instance.InstanceMask = 0xffu;
        instance.AccelerationStructure =
            bottomLevel_->GetGPUVirtualAddress();
        if (!CreateUploadBuffer(
                graphics.device,
                instances,
                instanceBuffer_)) {
            SetMessage(
                outMessage,
                "Failed to upload DXR instance data.");
            Reset();
            return false;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS
            topInputs{};
        topInputs.Type =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        topInputs.DescsLayout =
            D3D12_ELEMENTS_LAYOUT_ARRAY;
        topInputs.Flags =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        topInputs.NumDescs = 1u;
        topInputs.InstanceDescs =
            instanceBuffer_->GetGPUVirtualAddress();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO
            topInfo{};
        device->GetRaytracingAccelerationStructurePrebuildInfo(
            &topInputs,
            &topInfo);
        if (topInfo.ResultDataMaxSizeInBytes == 0u ||
            !CreateBuffer(
                graphics.device,
                AlignUp(
                    topInfo.ScratchDataSizeInBytes,
                    kAccelerationStructureAlignment),
                D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                topScratch_.GetAddressOf()) ||
            !CreateBuffer(
                graphics.device,
                AlignUp(
                    topInfo.ResultDataMaxSizeInBytes,
                    kAccelerationStructureAlignment),
                D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                topLevel_.GetAddressOf())) {
            SetMessage(
                outMessage,
                "Failed to allocate DXR top-level acceleration structure.");
            Reset();
            return false;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC
            topBuild{};
        topBuild.Inputs = topInputs;
        topBuild.ScratchAccelerationStructureData =
            topScratch_->GetGPUVirtualAddress();
        topBuild.DestAccelerationStructureData =
            topLevel_->GetGPUVirtualAddress();
        commandList->BuildRaytracingAccelerationStructure(
            &topBuild,
            0u,
            nullptr);

        D3D12_RESOURCE_BARRIER topBarrier{};
        topBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        topBarrier.UAV.pResource = topLevel_.Get();
        commandList->ResourceBarrier(1u, &topBarrier);
        SetMessage(outMessage, "DXR acceleration structures built.");
        return true;
    }

    void DxrTriangleAccelerationStructure::Reset() {
        topLevel_.Reset();
        topScratch_.Reset();
        bottomLevel_.Reset();
        bottomScratch_.Reset();
        instanceBuffer_.Reset();
        indexBuffer_.Reset();
        vertexBuffer_.Reset();
    }

    bool DxrTriangleAccelerationStructure::IsReady() const noexcept {
        return topLevel_ != nullptr;
    }

    D3D12_GPU_VIRTUAL_ADDRESS
        DxrTriangleAccelerationStructure::
            GetTopLevelGpuAddress() const noexcept {

        return topLevel_ != nullptr
            ? topLevel_->GetGPUVirtualAddress()
            : 0u;
    }

} // namespace HIKARI::GFX::RAYTRACING
