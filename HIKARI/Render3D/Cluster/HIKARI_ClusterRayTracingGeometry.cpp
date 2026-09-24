#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Cluster/HIKARI_ClusterRayTracingGeometry.h"
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Assets/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"

#include <algorithm>
#include <cstring>
#include <d3dx12.h>

namespace HIKARI::RENDER3D::CLUSTER {
bool BuildClusterRayTracingChunks(const ClusterGeometryResourceRecord& source, uint32_t maximumTrianglesPerChunk,
                                  uint32_t maximumRangesPerChunk, uint32_t maximumChunkCount,
                                  std::vector<ClusterRayTracingChunk>& output) {
    output.clear();
    if (!source.ready || source.surfaceRanges.empty() || maximumTrianglesPerChunk == 0 ||
        maximumTrianglesPerChunk > UINT32_MAX / 3 || maximumRangesPerChunk == 0 || maximumRangesPerChunk > (1u << 24) ||
        maximumChunkCount == 0)
        return false;
    std::vector<ClusterRayTracingChunk> chunks;
    uint64_t previousPrimitiveEnd = 0;
    for (size_t surfaceIndex = 0; surfaceIndex < source.surfaceRanges.size(); ++surfaceIndex) {
        const auto& surface = source.surfaceRanges[surfaceIndex];
        if (surface.surfaceIndex != surfaceIndex || surface.primitiveCount == 0 || surface.clusterCount == 0 ||
            surface.firstPrimitive < previousPrimitiveEnd ||
            uint64_t(surface.firstPrimitive) + surface.primitiveCount > source.layout.meshletPrimitiveCount ||
            uint64_t(surface.firstCluster) + surface.clusterCount > source.layout.clusterCount)
            return false;
        previousPrimitiveEnd = uint64_t(surface.firstPrimitive) + surface.primitiveCount;
        uint32_t offset = 0;
        while (offset < surface.primitiveCount) {
            if (chunks.empty() || chunks.back().primitiveCount == maximumTrianglesPerChunk ||
                chunks.back().ranges.size() == maximumRangesPerChunk) {
                if (chunks.size() == maximumChunkCount)
                    return false;
                chunks.emplace_back();
            }
            auto& chunk = chunks.back();
            const uint32_t count =
                (std::min)(maximumTrianglesPerChunk - chunk.primitiveCount, surface.primitiveCount - offset);
            chunk.ranges.push_back({surface.surfaceIndex, surface.firstPrimitive + offset, count, surface.firstCluster,
                                    surface.clusterCount});
            chunk.primitiveCount += count;
            offset += count;
        }
    }
    output = std::move(chunks);
    return true;
}

void ClusterRayTracingGeometry::Reset(const GFX::Context& graphics) {
    GFX::RetireD3D12ObjectForFrame(indices, graphics, "RT.Cluster.Indices");
    GFX::RetireD3D12ObjectForFrame(geometry, graphics, "RT.Cluster.Source");
    layout = {};
    sourceRevision = deviceGeneration = indexAllocationBytes = 0;
}

bool ClusterRayTracingGeometry::DescribeTriangles(uint32_t firstPrimitive, uint32_t primitiveCount,
                                                  D3D12_RAYTRACING_GEOMETRY_DESC& output) const {
    output = {};
    if (!indices || !geometry || primitiveCount == 0 || firstPrimitive > layout.meshletPrimitiveCount ||
        primitiveCount > layout.meshletPrimitiveCount - firstPrimitive)
        return false;
    output.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    // alpha / sidedness は TLAS instance flags と共通 hit-material evaluator が決める。
    output.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
    output.Triangles.VertexBuffer = {geometry->GetGPUVirtualAddress() + layout.vertexOffsetBytes,
                                     sizeof(ClusterGeometryGpuVertexPosition)};
    output.Triangles.VertexCount = layout.vertexCount;
    output.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    output.Triangles.IndexBuffer = indices->GetGPUVirtualAddress() + uint64_t(firstPrimitive) * 12;
    output.Triangles.IndexCount = primitiveCount * 3;
    output.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    return true;
}

ClusterRayTracingGeometryBuilder::~ClusterRayTracingGeometryBuilder() { Reset(GFX::GetCurrentContext()); }

void ClusterRayTracingGeometryBuilder::Reset(const GFX::Context& graphics) {
    GFX::RetireD3D12ObjectForFrame(pipeline_, graphics, "RT.Cluster.PSO");
    GFX::RetireD3D12ObjectForFrame(root_, graphics, "RT.Cluster.Root");
    device_ = nullptr;
    deviceGeneration_ = 0;
}

bool ClusterRayTracingGeometryBuilder::EnsurePipeline(const GFX::Context& graphics) {
    if (device_ != graphics.device || deviceGeneration_ != graphics.deviceGeneration)
        Reset(graphics);
    if (pipeline_)
        return true;
    CD3DX12_ROOT_PARAMETER parameters[4];
    parameters[0].InitAsShaderResourceView(0);
    parameters[1].InitAsShaderResourceView(1);
    parameters[2].InitAsUnorderedAccessView(0);
    parameters[3].InitAsConstants(4, 0);
    const CD3DX12_ROOT_SIGNATURE_DESC description(4, parameters);
    if (FAILED(GFX::CreateRootSignature(graphics.device, description, &root_)))
        return false;
    namespace SHADER = ASSETS::SHADER;
    const auto caps = GFX::QueryShaderDeviceCapabilities(graphics.device);
    const auto contract =
        SHADER::MakeRenderGeometryShaderContract(SHADER::RenderGeometryRootLayout::ClusterRayTracing,
                                                 {caps.shaderModel, caps.rootSignatureVersion, caps.featureMask});
    const auto shader = SHADER::ResolveBuiltinShaderEntry("HIKARI.EngineShader.Render3D_ClusterRayTracingCS",
                                                          {"Main", SHADER::ShaderStage::Compute, "cs_6_0"}, contract);
    if (!shader) {
        Reset(graphics);
        return false;
    }
    const auto dxil = shader.lease.GetDxil();
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = root_.Get();
    pipeline.CS = {dxil.data(), dxil.size()};
    if (FAILED(graphics.device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&pipeline_)))) {
        Reset(graphics);
        return false;
    }
    device_ = graphics.device;
    deviceGeneration_ = graphics.deviceGeneration;
    return true;
}

bool ClusterRayTracingGeometryBuilder::Build(const GFX::Context& graphics, const ClusterGeometryResourceRecord& source,
                                             ID3D12Resource* geometry, uint64_t maximumIndexAllocationBytes,
                                             ClusterRayTracingGeometry& output) {
    const auto fail = [&] {
        output.Reset(graphics);
        return false;
    };
    const auto& layout = source.layout;
    if (!graphics.device || !graphics.cmdList || !source.ready || !geometry || !source.metadataBuffer ||
        !layout.vertexCount || !layout.clusterCount || !layout.meshletPrimitiveCount ||
        layout.meshletPrimitiveCount > UINT32_MAX / 12 || maximumIndexAllocationBytes == 0)
        return fail();
    const auto geometryDesc = geometry->GetDesc();
    const auto metadataDesc = source.metadataBuffer->GetDesc();
    if (geometryDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        metadataDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || geometryDesc.Width < layout.byteSize ||
        metadataDesc.Width < sizeof(ClusterGeometryGpuHeader) ||
        uint64_t(layout.vertexOffsetBytes) + uint64_t(layout.vertexCount) * sizeof(ClusterGeometryGpuVertexPosition) >
            geometryDesc.Width ||
        uint64_t(layout.meshletPrimitiveOffsetBytes) + uint64_t(layout.meshletPrimitiveCount) * 4 >
            geometryDesc.Width ||
        uint64_t(layout.clusterOffsetBytes) + uint64_t(layout.clusterCount) * sizeof(ClusterGeometryGpuCluster) >
            metadataDesc.Width)
        return fail();
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(uint64_t(layout.meshletPrimitiveCount) * 12,
                                                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto allocation = graphics.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    if (allocation == UINT64_MAX || allocation > maximumIndexAllocationBytes)
        return fail();
    if (output.indices && output.geometry.Get() == geometry && output.sourceRevision == source.sourceRevision &&
        output.deviceGeneration == graphics.deviceGeneration && device_ == graphics.device &&
        std::memcmp(&output.layout, &layout, sizeof(layout)) == 0)
        return true;
    Microsoft::WRL::ComPtr<ID3D12Device> geometryDevice, metadataDevice;
    if (FAILED(geometry->GetDevice(IID_PPV_ARGS(&geometryDevice))) ||
        FAILED(source.metadataBuffer->GetDevice(IID_PPV_ARGS(&metadataDevice))) ||
        geometryDevice.Get() != graphics.device || metadataDevice.Get() != graphics.device)
        return fail();
    if (!EnsurePipeline(graphics))
        return fail();
    Microsoft::WRL::ComPtr<ID3D12Resource> indices;
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(graphics.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                        IID_PPV_ARGS(&indices))))
        return fail();
    const uint32_t dispatchWidth = (std::min)(layout.clusterCount, 65535u);
    const uint32_t constants[]{layout.clusterCount, layout.meshletPrimitiveCount, layout.vertexCount, dispatchWidth};
    graphics.cmdList->SetComputeRootSignature(root_.Get());
    graphics.cmdList->SetPipelineState(pipeline_.Get());
    graphics.cmdList->SetComputeRootShaderResourceView(0, geometry->GetGPUVirtualAddress());
    graphics.cmdList->SetComputeRootShaderResourceView(1, source.metadataBuffer->GetGPUVirtualAddress());
    graphics.cmdList->SetComputeRootUnorderedAccessView(2, indices->GetGPUVirtualAddress());
    graphics.cmdList->SetComputeRoot32BitConstants(3, 4, constants, 0);
    graphics.cmdList->Dispatch(dispatchWidth, (layout.clusterCount - 1) / dispatchWidth + 1, 1);
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(indices.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    graphics.cmdList->ResourceBarrier(1, &barrier);
    output.Reset(graphics);
    output.geometry = geometry;
    output.indices = std::move(indices);
    output.layout = layout;
    output.sourceRevision = source.sourceRevision;
    output.deviceGeneration = graphics.deviceGeneration;
    output.indexAllocationBytes = allocation;
    auto retainedMetadata = source.metadataBuffer;
    GFX::RetireD3D12ObjectForFrame(retainedMetadata, graphics, "RT.Cluster.Metadata");
    return true;
}
} // namespace HIKARI::RENDER3D::CLUSTER
