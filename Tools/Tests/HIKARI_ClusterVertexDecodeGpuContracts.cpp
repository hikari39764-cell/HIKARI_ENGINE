#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include "RenderContracts/Geometry/HIKARI_ClusterGeometryPacked.h"
#include "Render3D/Cluster/HIKARI_ClusterRayTracingGeometry.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Internal/HIKARI_GpuContractDevice.h"

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    using Microsoft::WRL::ComPtr;
    using namespace HIKARI;
    using namespace RENDER3D::CLUSTER;
    std::array<wchar_t, 32768> executable{};
    assert(GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size())) != 0);
    assert(CONTENT::GetEngineContentContext().Open(std::filesystem::path(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT)));
    ComPtr<ID3D12Debug> debug;
    assert(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))));
    debug->EnableDebugLayer();
    TESTS::INTERNAL::GpuContractDevice gpu;
    ComPtr<ID3D12InfoQueue> messages;
    assert(SUCCEEDED(gpu.device.As(&messages)));

    // UV0 の繰り返し/負値と、独立した UV1 の atlas 座標を実際の packer に渡す。
    // 奇数頂点数で position stream と attributes の境界も検証する。
    constexpr UINT vertexCount = 33;
    ClusteredGeometryAsset asset;
    asset.valid = true;
    asset.surfaces.resize(1);
    asset.clusters.resize(2);
    asset.clusters[0].firstVertex = 0;
    asset.clusters[0].vertexCount = 3;
    asset.clusters[0].firstPrimitive = 0;
    asset.clusters[0].triangleCount = 1;
    asset.clusters[1].firstVertex = 17;
    asset.clusters[1].vertexCount = 4;
    asset.clusters[1].firstPrimitive = 1;
    asset.clusters[1].triangleCount = 2;
    asset.packedVertices.resize(vertexCount);
    asset.meshletPrimitives = {{0, 1, 2, 0}, {1, 2, 3, 0}, {0, 2, 1, 0}};
    asset.packedIndices = {0, 1, 2};
    constexpr std::array<float, 6> repeatedUv{-21.375f, -0.125f, 0.f, 0.5f, 7.5f, 32.f};
    for (UINT i = 0; i < vertexCount; ++i) {
        auto& vertex = asset.packedVertices[i];
        vertex.position = {float(i), 2.f, -3.f};
        vertex.normal = {0.f, 1.f, 0.f};
        vertex.tangent = {1.f, 0.f, 0.f, -1.f};
        vertex.uv0 = {repeatedUv[(i + 1) % repeatedUv.size()], repeatedUv[i % repeatedUv.size()]};
        vertex.uv1 = {float(i + 1) / 64.f, float(63 - i) / 64.f};
    }
    ClusterGeometryPackedBytes packed;
    std::string message;
    assert(PackClusterGeometryForGpu(asset, {false}, packed, &message));
    const char* source = R"(
        #include "Include/Cluster/HIKARI_ClusterGpuData.hlsli"
        ByteAddressBuffer geometry : register(t0);
        RWStructuredBuffer<float4> output : register(u0);
        [numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) {
            const HikariClusterGeometryHeader header = HikariLoadClusterGeometryHeader(geometry);
            if (id.x >= header.vertexCount) return;
            output[id.x * 4] = HikariLoadClusterVertex(geometry, header, id.x).uv01;
            output[id.x * 4 + 1] = HikariLoadClusterVertexShading(geometry, header, id.x).uv01;
            const float4 uv = HikariLoadClusterVertexUv01(geometry, header, id.x);
            output[id.x * 4 + 2] = uv;
            // atlas scale/bias 後のサンプル位置。UV0 との混線で隣の allocation に入らないこと。
            output[id.x * 4 + 3] = float4(uv.zw * float2(.25,.5) + float2(.125,.25), 0, 1);
        }
    )";
    const auto shaderRoot = std::filesystem::path(HIKARI_CONTRACT_SOURCE_ROOT) / "HIKARI/Shaders";
    std::filesystem::current_path(shaderRoot);
    const auto sourceName = (shaderRoot / "ClusterVertexDecodeContract.hlsl").string();
    ComPtr<ID3DBlob> shader, errors, serialized;
    const HRESULT compiled =
        D3DCompile(source, std::strlen(source), sourceName.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main",
                   "cs_5_0", D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &shader, &errors);
    if (FAILED(compiled) && errors)
        std::fputs(static_cast<const char*>(errors->GetBufferPointer()), stderr);
    assert(SUCCEEDED(compiled));
    std::array<CD3DX12_ROOT_PARAMETER, 2> parameters;
    parameters[0].InitAsShaderResourceView(0);
    parameters[1].InitAsUnorderedAccessView(0);
    const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(static_cast<UINT>(parameters.size()), parameters.data());
    assert(SUCCEEDED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors)));
    ComPtr<ID3D12RootSignature> root;
    assert(SUCCEEDED(gpu.device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))));
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = root.Get();
    psoDesc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;
    assert(SUCCEEDED(gpu.device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipeline))));
    constexpr UINT outputBytes = vertexCount * 4 * sizeof(MATH::Vec4);
    const auto inputDesc = CD3DX12_RESOURCE_DESC::Buffer(packed.geometryBytes.size());
    const auto outputDesc = CD3DX12_RESOURCE_DESC::Buffer(outputBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(outputBytes);
    const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD), defaultHeap(D3D12_HEAP_TYPE_DEFAULT),
        readbackHeap(D3D12_HEAP_TYPE_READBACK);
    ComPtr<ID3D12Resource> input, output, readback;
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &inputDesc,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                         IID_PPV_ARGS(&input))));
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &outputDesc,
                                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                         IID_PPV_ARGS(&output))));
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&readback))));
    void* upload = nullptr;
    const D3D12_RANGE noRead{0, 0};
    assert(SUCCEEDED(input->Map(0, &noRead, &upload)));
    std::memcpy(upload, packed.geometryBytes.data(), packed.geometryBytes.size());
    input->Unmap(0, nullptr);
    gpu.ResetCommands();
    gpu.commands->SetComputeRootSignature(root.Get());
    gpu.commands->SetPipelineState(pipeline.Get());
    gpu.commands->SetComputeRootShaderResourceView(0, input->GetGPUVirtualAddress());
    gpu.commands->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
    gpu.commands->Dispatch(1, 1, 1);
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                              D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.commands->ResourceBarrier(1, &barrier);
    gpu.commands->CopyResource(readback.Get(), output.Get());
    gpu.SubmitAndWait(1);
    void* mapped = nullptr;
    assert(SUCCEEDED(readback->Map(0, nullptr, &mapped)));
    const auto* actual = static_cast<const MATH::Vec4*>(mapped);
    bool valid = true;
    for (UINT i = 0; i < vertexCount; ++i) {
        const auto& vertex = asset.packedVertices[i];
        const MATH::Vec4 uv{vertex.uv0.x, vertex.uv0.y, vertex.uv1.x, vertex.uv1.y};
        const MATH::Vec4 atlas{vertex.uv1.x * .25f + .125f, vertex.uv1.y * .5f + .25f, 0, 1};
        for (UINT path = 0; path < 4; ++path) {
            const auto expected = path == 3 ? atlas : uv;
            if (std::memcmp(&actual[i * 4 + path], &expected, sizeof(expected)) != 0) {
                std::fprintf(stderr,
                             "Cluster UV decode vertex %u path %u: actual=(%g,%g,%g,%g) expected=(%g,%g,%g,%g)\n", i,
                             path, actual[i * 4 + path].x, actual[i * 4 + path].y, actual[i * 4 + path].z,
                             actual[i * 4 + path].w, expected.x, expected.y, expected.z, expected.w);
                valid = false;
            }
        }
    }
    readback->Unmap(0, &noRead);
    // 同じ製品 packer / cooked shader / GPU 変換 API で RT の global index を検証。
    GFX::GpuDeferredReleaseQueue retired;
    GFX::Context context{};
    context.device = gpu.device.Get();
    context.cmdList = gpu.commands.Get();
    context.queue = gpu.queue.Get();
    context.frameFence = gpu.fence.Get();
    context.deferredReleaseQueue = &retired;
    context.currentFrameRetireFenceValue = 2;
    GFX::PublishCurrentContext(context);
    RENDER3D::ClusterGeometryResourceRecord record;
    record.ready = true;
    record.layout = packed.layout;
    record.sourceRevision = 1;
    auto planRecord = record;
    planRecord.layout.meshletPrimitiveCount = 20;
    planRecord.layout.clusterCount = 4;
    planRecord.surfaceRanges.resize(2);
    planRecord.surfaceRanges[0].primitiveCount = 7;
    planRecord.surfaceRanges[0].clusterCount = 1;
    planRecord.surfaceRanges[1].surfaceIndex = 1;
    planRecord.surfaceRanges[1].firstPrimitive = 15;
    planRecord.surfaceRanges[1].primitiveCount = 5;
    planRecord.surfaceRanges[1].firstCluster = 2;
    planRecord.surfaceRanges[1].clusterCount = 1;
    std::vector<ClusterRayTracingChunk> chunks;
    assert(BuildClusterRayTracingChunks(planRecord, 4, 2, 4, chunks));
    assert(chunks.size() == 3 && chunks[1].ranges.size() == 2);
    std::vector<uint32_t> plannedPrimitives;
    for (const auto& chunk : chunks) {
        assert(chunk.primitiveCount == 4);
        for (const auto& range : chunk.ranges)
            for (uint32_t i = 0; i < range.primitiveCount; ++i)
                plannedPrimitives.push_back(range.firstPrimitive + i);
    }
    assert((plannedPrimitives == std::vector<uint32_t>{0, 1, 2, 3, 4, 5, 6, 15, 16, 17, 18, 19}));
    assert(BuildClusterRayTracingChunks(planRecord, 4, 1, 4, chunks) && chunks.size() == 4);
    assert(!BuildClusterRayTracingChunks(planRecord, 4, 2, 2, chunks) && chunks.empty());
    planRecord.surfaceRanges[1].firstPrimitive = 6;
    assert(!BuildClusterRayTracingChunks(planRecord, 4, 2, 4, chunks) && chunks.empty());
    assert(record.layout.indexCount == 0); // Mesh Shader 専用 cook も再 import しない。
    assert(GFX::D3D12_BUFFER::CreateMappedUploadBuffer(gpu.device.Get(), packed.metadataBytes.size(),
                                                       record.metadataBuffer, &mapped));
    std::memcpy(mapped, packed.metadataBytes.data(), packed.metadataBytes.size());
    record.metadataBuffer->Unmap(0, nullptr);
    ClusterRayTracingGeometryBuilder converter;
    ClusterRayTracingGeometry rtGeometry;
    constexpr std::array<uint32_t, 9> expectedIndices{0, 1, 2, 18, 19, 20, 17, 19, 18};
    const auto rtReadDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(expectedIndices));
    ComPtr<ID3D12Resource> rtReadback;
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &rtReadDesc,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&rtReadback))));
    gpu.ResetCommands();
    assert(!converter.Build(context, record, input.Get(), 1, rtGeometry));
    assert(converter.Build(context, record, input.Get(), 1024 * 1024, rtGeometry));
    const auto originalIndices = rtGeometry.indices->GetGPUVirtualAddress();
    assert(rtGeometry.geometry.Get() == input.Get() && rtGeometry.indexAllocationBytes <= 1024 * 1024);
    assert(converter.Build(context, record, input.Get(), 1024 * 1024, rtGeometry));
    assert(rtGeometry.indices->GetGPUVirtualAddress() == originalIndices);
    D3D12_RAYTRACING_GEOMETRY_DESC rtDesc{};
    assert(rtGeometry.DescribeTriangles(1, 2, rtDesc));
    assert(rtDesc.Triangles.IndexCount == 6 && rtDesc.Triangles.IndexBuffer == originalIndices + 12);
    assert(rtDesc.Triangles.VertexBuffer.StartAddress ==
           input->GetGPUVirtualAddress() + packed.layout.vertexOffsetBytes);
    assert(!rtGeometry.DescribeTriangles(3, 1, rtDesc) && rtDesc.Triangles.IndexCount == 0);
    D3D12_RESOURCE_BARRIER rtBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        rtGeometry.indices.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.commands->ResourceBarrier(1, &rtBarrier);
    gpu.commands->CopyBufferRegion(rtReadback.Get(), 0, rtGeometry.indices.Get(), 0, sizeof(expectedIndices));
    std::swap(rtBarrier.Transition.StateBefore, rtBarrier.Transition.StateAfter);
    gpu.commands->ResourceBarrier(1, &rtBarrier);
    ++record.sourceRevision;
    assert(converter.Build(context, record, input.Get(), 1024 * 1024, rtGeometry));
    assert(rtGeometry.indices->GetGPUVirtualAddress() != originalIndices);
    record.layout.vertexCount = UINT32_MAX;
    assert(!converter.Build(context, record, input.Get(), 1024 * 1024, rtGeometry));
    assert(!rtGeometry.indices && !rtGeometry.geometry && rtGeometry.indexAllocationBytes == 0);
    converter.Reset(context);
    assert(retired.GetPendingCount() > 0);
    gpu.SubmitAndWait(2);
    retired.Collect(2);
    assert(SUCCEEDED(rtReadback->Map(0, nullptr, &mapped)));
    assert(std::memcmp(mapped, expectedIndices.data(), sizeof(expectedIndices)) == 0);
    rtReadback->Unmap(0, &noRead);
    GFX::ClearCurrentContext();
    CONTENT::GetEngineContentContext().Close();
    std::puts(
        "Cluster RT conversion PASS: GPU global indices, no fallback stream, shared vertices, revision cache, budgets, retirement.");
    for (UINT64 i = 0; i < messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
        SIZE_T size = 0;
        messages->GetMessage(i, nullptr, &size);
        std::vector<std::byte> storage(size);
        auto* diagnostic = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        assert(SUCCEEDED(messages->GetMessage(i, diagnostic, &size)));
        if (diagnostic->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
            std::fputs(diagnostic->pDescription, stderr);
            valid = false;
        }
    }
    if (!valid)
        return 1;
    std::puts(
        "Cluster vertex decode PASS: production packer, 33 independent UV0/UV1 values, all three loaders and atlas coordinates.");
    return 0;
}
