#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingDeformation.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"

namespace HIKARI::RENDER3D::RAYTRACING {
namespace {
constexpr const wchar_t* kShaderPath = L"HIKARI/Shaders/Render3D_RayTracingDeformCS.hlsl";
constexpr UINT kJobConstantCount = 20u;
constexpr uint32_t kThreadsPerGroup = 64u;
constexpr D3D12_RESOURCE_STATES kReadState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;

// shader の RayTracingDeformJob と同じ並び（world の 1..3 行、番号、数、骨格、出力位置、種類）。
std::array<uint32_t, kJobConstantCount> PackJob(const RuntimeRayTracingDeformJob& job) {
    std::array<uint32_t, kJobConstantCount> constants{};
    std::array<float, 12> rows{};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
            rows[row * 4 + column] = job.world.m[column][row];
    std::memcpy(constants.data(), rows.data(), sizeof(rows));
    constants[12] = job.geometryDescriptor;
    constants[13] = job.vertexCount;
    constants[14] = job.paletteOffsetBytes;
    constants[15] = job.paletteMatrixCount;
    constants[16] = job.outputBase;
    constants[17] = job.kind;
    return constants;
}
} // namespace

RuntimeRayTracingDeformation::~RuntimeRayTracingDeformation() { Reset(GFX::GetCurrentContext()); }

bool RuntimeRayTracingDeformation::EnsurePipeline(const GFX::Context& graphics) {
    if (deviceGeneration_ != graphics.deviceGeneration) {
        root_.Reset();
        pipeline_.Reset();
        pipelineFailed_ = false;
        deviceGeneration_ = graphics.deviceGeneration;
    }
    if (pipeline_)
        return true;
    if (pipelineFailed_ || !GFX::SupportsShaderModel6(graphics.device))
        return false;
    pipelineFailed_ = true;
    std::array<CD3DX12_ROOT_PARAMETER, 6> parameters;
    CD3DX12_DESCRIPTOR_RANGE geometry;
    geometry.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaCapacity, 0, 1);
    parameters[0].InitAsConstants(kJobConstantCount, 0);
    parameters[1].InitAsShaderResourceView(0, 3); // 骨格の行列（描画と同じ upload arena）
    parameters[2].InitAsDescriptorTable(1, &geometry);
    parameters[3].InitAsUnorderedAccessView(0);
    parameters[4].InitAsShaderResourceView(4, 7); // 地形の記録（描画と同じ t4,space7）
    parameters[5].InitAsShaderResourceView(1, 7); // 地形の高さ場の float pool（t1,space7）
    const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(parameters.size()), parameters.data());
    if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(graphics.device, rootDesc, root_.GetAddressOf()),
                         "RayTracingDeform root signature"))
        return false;
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    if (!GFX::CompileShaderFileSm6(kShaderPath, "CSMain", L"cs_6_5", shader.GetAddressOf())) {
        DEBUGLOG::PushRenderError("[RT][Deform] compute shader compile failed; skinned surfaces stay excluded.");
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root_.Get();
    desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
    if (!HIKARI_DX_CHECK(graphics.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(pipeline_.GetAddressOf())),
                         "RayTracingDeform pipeline"))
        return false;
    pipelineFailed_ = false;
    return true;
}

bool RuntimeRayTracingDeformation::EnsurePool(const GFX::Context& graphics, uint64_t bytes) {
    if (pool_ && poolBytes_ >= bytes)
        return true;
    Reset(graphics);
    // 少し余裕を持たせ、角色が一人増えるたびに作り直さない。
    const uint64_t size = (std::max<uint64_t>)(bytes + bytes / 2, 1ull << 20);
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (FAILED(graphics.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                        IID_PPV_ARGS(pool_.GetAddressOf())))) {
        pool_.Reset();
        return false;
    }
    pool_->SetName(L"HIKARI.RT.DeformedVertices");
    poolBytes_ = size;
    poolState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    return true;
}

bool RuntimeRayTracingDeformation::Dispatch(const GFX::Context& graphics,
                                            std::span<const RuntimeRayTracingDeformJob> jobs,
                                            D3D12_GPU_VIRTUAL_ADDRESS palettes,
                                            D3D12_GPU_VIRTUAL_ADDRESS terrainRecords,
                                            D3D12_GPU_VIRTUAL_ADDRESS surfaceFloats) {
    if (jobs.empty())
        return true;
    bool skinned = false, terrain = false;
    for (const RuntimeRayTracingDeformJob& job : jobs) {
        skinned = skinned || job.kind == kRuntimeRayTracingDeformSkinned;
        terrain = terrain || job.kind == kRuntimeRayTracingDeformTerrain;
    }
    if (!pool_ || (skinned && palettes == 0) || (terrain && (terrainRecords == 0 || surfaceFloats == 0)) ||
        graphics.cmdList == nullptr || graphics.srvHeap == nullptr || !EnsurePipeline(graphics))
        return false;
    auto* commands = graphics.cmdList;
    GFX::PIX::ScopedGpuEvent event(commands, GFX::PIX::kColorRender, "RayTracing.Deform");
    if (poolState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        const auto barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(pool_.Get(), poolState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands->ResourceBarrier(1, &barrier);
        poolState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    ID3D12DescriptorHeap* heaps[]{graphics.srvHeap};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(root_.Get());
    commands->SetPipelineState(pipeline_.Get());
    const UINT stride = graphics.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // 使わない種類の資源も有効な番地を束ねる（pool は常にある）。
    const D3D12_GPU_VIRTUAL_ADDRESS fallback = pool_->GetGPUVirtualAddress();
    commands->SetComputeRootShaderResourceView(1, palettes != 0 ? palettes : fallback);
    commands->SetComputeRootDescriptorTable(
        2, GFX::DESCRIPTOR::GpuAt(graphics.srvHeap, stride, GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaBegin));
    commands->SetComputeRootUnorderedAccessView(3, pool_->GetGPUVirtualAddress());
    commands->SetComputeRootShaderResourceView(4, terrainRecords != 0 ? terrainRecords : fallback);
    commands->SetComputeRootShaderResourceView(5, surfaceFloats != 0 ? surfaceFloats : fallback);
    for (const RuntimeRayTracingDeformJob& job : jobs) {
        if (job.vertexCount == 0)
            continue;
        const auto constants = PackJob(job);
        commands->SetComputeRoot32BitConstants(0, kJobConstantCount, constants.data(), 0);
        commands->Dispatch((job.vertexCount + kThreadsPerGroup - 1) / kThreadsPerGroup, 1, 1);
    }
    const std::array barriers{
        CD3DX12_RESOURCE_BARRIER::UAV(pool_.Get()),
        CD3DX12_RESOURCE_BARRIER::Transition(pool_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kReadState)};
    commands->ResourceBarrier(UINT(barriers.size()), barriers.data());
    poolState_ = kReadState;
    return true;
}

D3D12_GPU_VIRTUAL_ADDRESS RuntimeRayTracingDeformation::PoolAddress() const noexcept {
    return pool_ ? pool_->GetGPUVirtualAddress() : 0;
}

void RuntimeRayTracingDeformation::Reset(const GFX::Context& graphics) {
    GFX::RetireD3D12ObjectForFrame(pool_, graphics, "RT.DeformedVertices");
    poolBytes_ = 0;
    poolState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

} // namespace HIKARI::RENDER3D::RAYTRACING
