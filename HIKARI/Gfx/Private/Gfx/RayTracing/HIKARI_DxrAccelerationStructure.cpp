#include "Gfx/RayTracing/HIKARI_DxrAccelerationStructure.h"
#include "Core/Text/HIKARI_OutMessage.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <d3dx12.h>

namespace HIKARI::GFX::RAYTRACING {
namespace {
constexpr uint64_t kAlignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;

bool ValidTriangles(const D3D12_RAYTRACING_GEOMETRY_DESC& geometry) {
    if (geometry.Type != D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES)
        return false;
    const auto& triangles = geometry.Triangles;
    if (triangles.VertexCount == 0 || triangles.VertexBuffer.StartAddress == 0 ||
        triangles.VertexBuffer.StartAddress % 4 != 0 || triangles.VertexBuffer.StrideInBytes < 12 ||
        triangles.VertexBuffer.StrideInBytes % 4 != 0 || triangles.VertexFormat != DXGI_FORMAT_R32G32B32_FLOAT ||
        triangles.Transform3x4 % 16 != 0 ||
        (geometry.Flags &
         ~(D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE | D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION)) != 0)
        return false;
    if (triangles.IndexFormat == DXGI_FORMAT_UNKNOWN)
        return triangles.IndexCount == 0 && triangles.IndexBuffer == 0 && triangles.VertexCount % 3 == 0;
    const uint32_t alignment = triangles.IndexFormat == DXGI_FORMAT_R16_UINT ? 2u : 4u;
    return (triangles.IndexFormat == DXGI_FORMAT_R16_UINT || triangles.IndexFormat == DXGI_FORMAT_R32_UINT) &&
           triangles.IndexBuffer != 0 && triangles.IndexBuffer % alignment == 0 && triangles.IndexCount != 0 &&
           triangles.IndexCount % 3 == 0;
}

// 不透明の強制と非不透明の強制は同時に指定できない。
bool ValidInstances(std::span<const D3D12_RAYTRACING_INSTANCE_DESC> instances) {
    constexpr UINT kForcedOpacityFlags =
        D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
    bool valid = instances.size() <= (1u << 24);
    for (const auto& instance : instances) {
        valid &= instance.AccelerationStructure % kAlignment == 0 &&
                 (instance.InstanceMask == 0 || instance.AccelerationStructure != 0) &&
                 (instance.Flags & ~0x0fu) == 0 && (instance.Flags & kForcedOpacityFlags) != kForcedOpacityFlags;
        for (const auto& row : instance.Transform)
            for (const float value : row)
                valid &= std::isfinite(value);
    }
    return valid;
}

template <class T>
bool Upload(ID3D12Device* device, std::span<const T> values, Microsoft::WRL::ComPtr<ID3D12Resource>& resource) {
    void* mapped = nullptr;
    if (!D3D12_BUFFER::CreateMappedUploadBuffer(device, values.size_bytes(), resource, &mapped))
        return false;
    std::memcpy(mapped, values.data(), values.size_bytes());
    resource->Unmap(0, nullptr);
    return true;
}
} // namespace

bool DxrAccelerationStructure::BuildBottomLevel(const Context& graphics,
                                                std::span<const D3D12_RAYTRACING_GEOMETRY_DESC> geometry,
                                                uint64_t maximumAllocationBytes, std::string* outMessage,
                                                bool allowUpdate) {
    if (geometry.empty() || geometry.size() > (1u << 24) ||
        !std::all_of(geometry.begin(), geometry.end(), ValidTriangles)) {
        Reset(&graphics);
        SetMessage(outMessage, "DXR triangle descriptors are invalid.");
        return false;
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (allowUpdate)
        inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    inputs.NumDescs = static_cast<UINT>(geometry.size());
    inputs.pGeometryDescs = geometry.data();
    return Build(graphics, inputs, {}, maximumAllocationBytes, nullptr, outMessage);
}

bool DxrAccelerationStructure::BuildTopLevel(const Context& graphics,
                                             std::span<const D3D12_RAYTRACING_INSTANCE_DESC> instances,
                                             uint64_t maximumAllocationBytes, bool allowUpdate,
                                             const DxrAccelerationStructure* updateSource, std::string* outMessage) {
    if (!ValidInstances(instances) || (updateSource != nullptr && !allowUpdate)) {
        Reset(&graphics);
        SetMessage(outMessage, "DXR instance descriptors or update request are invalid.");
        return false;
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (allowUpdate)
        inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    inputs.NumDescs = static_cast<UINT>(instances.size());
    return Build(graphics, inputs, instances, maximumAllocationBytes, updateSource, outMessage);
}

bool DxrAccelerationStructure::Build(const Context& graphics,
                                     D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs,
                                     std::span<const D3D12_RAYTRACING_INSTANCE_DESC> instances,
                                     uint64_t maximumAllocationBytes, const DxrAccelerationStructure* updateSource,
                                     std::string* outMessage) {
    const auto fail = [&](const char* message) {
        Reset(&graphics); // 不完全な scene に旧 TLAS を公開しない。
        SetMessage(outMessage, message);
        return false;
    };
    if (graphics.device == nullptr || graphics.cmdList == nullptr || maximumAllocationBytes == 0)
        return fail("DXR build context or allocation budget is unavailable.");
    if ((device_ != graphics.device || deviceGeneration_ != graphics.deviceGeneration) &&
        QueryDxrCapabilities(graphics.device).tier == DxrSupportTier::Unsupported)
        return fail("DXR acceleration structures are unsupported on this device.");
    Microsoft::WRL::ComPtr<ID3D12Device5> device;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commands;
    if (FAILED(graphics.device->QueryInterface(IID_PPV_ARGS(&device))) ||
        FAILED(graphics.cmdList->QueryInterface(IID_PPV_ARGS(&commands))))
        return fail("Required DXR device or command-list interface is unavailable.");
    if (updateSource != nullptr &&
        (!updateSource->IsReady() || updateSource->device_ != graphics.device ||
         updateSource->deviceGeneration_ != graphics.deviceGeneration || updateSource->type_ != inputs.Type ||
         updateSource->flags_ != inputs.Flags || updateSource->descriptorCount_ != inputs.NumDescs))
        return fail("DXR update source has incompatible type, device, count or build flags.");

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    if (info.ResultDataMaxSizeInBytes == 0)
        return fail("DXR prebuild returned no acceleration structure.");
    const uint64_t resultBytes = info.ResultDataMaxSizeInBytes;
    // 更新できる構造は、後の RefitInPlace / RebuildTopLevelInPlace でも同じ scratch を使えるよう
    // build と update の大きい方を取る。
    const bool updatable = (inputs.Flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE) != 0;
    const uint64_t scratchBytes =
        (std::max)({kAlignment, info.ScratchDataSizeInBytes, updatable ? info.UpdateScratchDataSizeInBytes : 0ull});
    if (resultBytes > UINT64_MAX - kAlignment || scratchBytes > UINT64_MAX - kAlignment)
        return fail("DXR prebuild size overflow.");
    const auto resultDesc = CD3DX12_RESOURCE_DESC::Buffer((resultBytes + kAlignment - 1) & ~(kAlignment - 1),
                                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto scratchDesc = CD3DX12_RESOURCE_DESC::Buffer((scratchBytes + kAlignment - 1) & ~(kAlignment - 1),
                                                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto instanceDesc = CD3DX12_RESOURCE_DESC::Buffer((std::max)(size_t{1}, instances.size_bytes()));
    uint64_t allocationBytes = 0, instanceBytes = 0;
    for (const auto* desc : {&resultDesc, &scratchDesc, &instanceDesc}) {
        if (desc == &instanceDesc && instances.empty())
            continue;
        const uint64_t bytes = device->GetResourceAllocationInfo(0, 1, desc).SizeInBytes;
        if (bytes == UINT64_MAX || bytes > maximumAllocationBytes - allocationBytes)
            return fail("DXR build exceeds its committed-memory budget.");
        allocationBytes += bytes;
        if (desc == &instanceDesc)
            instanceBytes = bytes;
    }

    // 再 build/refit でも旧結果を上書きしない。記録済み View と GPU 読み取りを保護する。
    Microsoft::WRL::ComPtr<ID3D12Resource> result, scratch, instanceBuffer;
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resultDesc,
                                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
                                               IID_PPV_ARGS(&result))) ||
        FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &scratchDesc,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                               IID_PPV_ARGS(&scratch))) ||
        (!instances.empty() && !Upload(graphics.device, instances, instanceBuffer)))
        return fail("Failed to allocate DXR build resources.");
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs = inputs;
    if (inputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL)
        build.Inputs.InstanceDescs = instanceBuffer ? instanceBuffer->GetGPUVirtualAddress() : 0;
    if (updateSource != nullptr) {
        build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        build.SourceAccelerationStructureData = updateSource->GetGpuAddress();
    }
    build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    build.DestAccelerationStructureData = result->GetGPUVirtualAddress();
    commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(result.Get());
    commands->ResourceBarrier(1, &barrier);
    Reset(&graphics);
    result_ = std::move(result);
    scratch_ = std::move(scratch);
    if (instanceBuffer)
        instances_.Adopt(graphics, std::move(instanceBuffer), instances.size_bytes(), instanceBytes, "DXR.Instances");
    device_ = graphics.device;
    deviceGeneration_ = graphics.deviceGeneration;
    allocationBytes_ = allocationBytes - instanceBytes;
    resultBytes_ = resultDesc.Width;
    scratchBytes_ = scratchDesc.Width;
    descriptorCount_ = inputs.NumDescs;
    type_ = inputs.Type;
    flags_ = inputs.Flags;
    SetMessage(outMessage, "DXR acceleration structure built.");
    return true;
}

bool DxrAccelerationStructure::RefitInPlace(const Context& graphics,
                                            std::span<const D3D12_RAYTRACING_GEOMETRY_DESC> geometry) {
    const bool bottom = type_ == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    if (!result_ || !scratch_ || graphics.cmdList == nullptr || device_ != graphics.device ||
        deviceGeneration_ != graphics.deviceGeneration ||
        (flags_ & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE) == 0 ||
        (bottom ? geometry.size() != descriptorCount_ || !std::all_of(geometry.begin(), geometry.end(), ValidTriangles)
                : !geometry.empty()))
        return false;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commands;
    if (FAILED(graphics.cmdList->QueryInterface(IID_PPV_ARGS(&commands))))
        return false;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs.Type = type_;
    build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build.Inputs.Flags = flags_ | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    build.Inputs.NumDescs = descriptorCount_;
    if (bottom) {
        build.Inputs.pGeometryDescs = geometry.data();
    } else {
        build.Inputs.InstanceDescs = instances_.Current() ? instances_.Current()->GetGPUVirtualAddress() : 0;
        instances_.MarkRead(graphics);
    }
    build.SourceAccelerationStructureData = result_->GetGPUVirtualAddress();
    build.DestAccelerationStructureData = result_->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress();
    commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(result_.Get());
    commands->ResourceBarrier(1, &barrier);
    return true;
}

bool DxrAccelerationStructure::RebuildTopLevelInPlace(const Context& graphics,
                                                      std::span<const D3D12_RAYTRACING_INSTANCE_DESC> instances) {
    if (!result_ || !scratch_ || type_ != D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL ||
        graphics.cmdList == nullptr || graphics.frameFence == nullptr || device_ != graphics.device ||
        deviceGeneration_ != graphics.deviceGeneration || instances.empty() || instances.size() != descriptorCount_ ||
        !ValidInstances(instances))
        return false;
    Microsoft::WRL::ComPtr<ID3D12Device5> device;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commands;
    if (FAILED(graphics.device->QueryInterface(IID_PPV_ARGS(&device))) ||
        FAILED(graphics.cmdList->QueryInterface(IID_PPV_ARGS(&commands))))
        return false;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs.Type = type_;
    build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build.Inputs.Flags = flags_;
    build.Inputs.NumDescs = descriptorCount_;
    // 見積もりが今の結果と scratch に収まる時だけその場で組む。
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&build.Inputs, &info);
    if (info.ResultDataMaxSizeInBytes == 0 || info.ResultDataMaxSizeInBytes > resultBytes_ ||
        info.ScratchDataSizeInBytes > scratchBytes_)
        return false;

    if (!instances_.Write(graphics, instances.data(), instances.size_bytes(), "DXR.Instances"))
        return false;

    // 先に積んだ光線の読み取りと、前の build の scratch 書き込みが終わってから上書きする。
    const D3D12_RESOURCE_BARRIER before[] = {CD3DX12_RESOURCE_BARRIER::UAV(result_.Get()),
                                             CD3DX12_RESOURCE_BARRIER::UAV(scratch_.Get())};
    commands->ResourceBarrier(2, before);
    build.Inputs.InstanceDescs = instances_.Current()->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress();
    build.DestAccelerationStructureData = result_->GetGPUVirtualAddress();
    commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    const auto after = CD3DX12_RESOURCE_BARRIER::UAV(result_.Get());
    commands->ResourceBarrier(1, &after);
    return true;
}

DxrAccelerationStructure::~DxrAccelerationStructure() { Reset(); }

void DxrAccelerationStructure::Reset(const Context* retireContext) {
    const auto& context = retireContext != nullptr ? *retireContext : GetCurrentContext();
    RetireD3D12ObjectForFrame(result_, context, "DXR.Result");
    RetireD3D12ObjectForFrame(scratch_, context, "DXR.Scratch");
    instances_.Reset(context, "DXR.Instances");
    device_ = nullptr;
    deviceGeneration_ = allocationBytes_ = resultBytes_ = scratchBytes_ = descriptorCount_ = 0;
    type_ = {};
    flags_ = {};
}

D3D12_GPU_VIRTUAL_ADDRESS DxrAccelerationStructure::GetGpuAddress() const noexcept {
    return result_ ? result_->GetGPUVirtualAddress() : 0;
}

bool DxrTriangleAccelerationStructure::Build(const Context& graphics, const std::vector<DxrPosition>& positions,
                                             const std::vector<uint32_t>& indices, std::string* outMessage) {
    Reset();
    if (graphics.device == nullptr || graphics.cmdList == nullptr || positions.empty() || indices.empty() ||
        positions.size() > UINT32_MAX || indices.size() > UINT32_MAX || indices.size() % 3 != 0 ||
        !std::all_of(positions.begin(), positions.end(),
                     [](const auto& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }) ||
        !std::all_of(indices.begin(), indices.end(), [&](uint32_t i) { return i < positions.size(); })) {
        SetMessage(outMessage, "DXR acceleration structure input is invalid.");
        return false;
    }
    if (!QueryDxrCapabilities(graphics.device).SupportsInlineRayQuery()) {
        SetMessage(outMessage, "DXR 1.1 with Shader Model 6.5 is unavailable.");
        return false;
    }
    if (!Upload<DxrPosition>(graphics.device, positions, vertexBuffer_) ||
        !Upload<uint32_t>(graphics.device, indices, indexBuffer_)) {
        SetMessage(outMessage, "Failed to upload DXR triangle geometry.");
        Reset();
        return false;
    }
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexBuffer = {vertexBuffer_->GetGPUVirtualAddress(), sizeof(DxrPosition)};
    geometry.Triangles.VertexCount = static_cast<UINT>(positions.size());
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.IndexBuffer = indexBuffer_->GetGPUVirtualAddress();
    geometry.Triangles.IndexCount = static_cast<UINT>(indices.size());
    geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    if (!bottomLevel_.BuildBottomLevel(graphics, {&geometry, 1}, UINT64_MAX, outMessage)) {
        Reset();
        return false;
    }
    D3D12_RAYTRACING_INSTANCE_DESC instance{};
    instance.Transform[0][0] = instance.Transform[1][1] = instance.Transform[2][2] = 1;
    instance.InstanceMask = 0xff;
    instance.AccelerationStructure = bottomLevel_.GetGpuAddress();
    if (!topLevel_.BuildTopLevel(graphics, {&instance, 1}, UINT64_MAX, false, nullptr, outMessage)) {
        Reset();
        return false;
    }
    return true;
}

DxrTriangleAccelerationStructure::~DxrTriangleAccelerationStructure() { Reset(); }

void DxrTriangleAccelerationStructure::Reset() {
    topLevel_.Reset();
    bottomLevel_.Reset();
    RetireD3D12ObjectForCurrentFrame(indexBuffer_, "DXR.Indices");
    RetireD3D12ObjectForCurrentFrame(vertexBuffer_, "DXR.Vertices");
}

bool DxrTriangleAccelerationStructure::IsReady() const noexcept { return topLevel_.IsReady(); }

D3D12_GPU_VIRTUAL_ADDRESS DxrTriangleAccelerationStructure::GetTopLevelGpuAddress() const noexcept {
    return topLevel_.GetGpuAddress();
}
} // namespace HIKARI::GFX::RAYTRACING
