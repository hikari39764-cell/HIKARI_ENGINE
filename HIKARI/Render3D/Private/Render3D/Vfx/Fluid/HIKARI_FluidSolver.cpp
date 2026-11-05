#include "Render3D/Vfx/Fluid/HIKARI_FluidSolver.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include <d3dx12.h>

#include "RenderContracts/Shader/HIKARI_FluidShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Gfx/HIKARI_VideoMemory.h"

namespace HIKARI::RENDER3D::FLUID::INTERNAL {
namespace {
using Microsoft::WRL::ComPtr;

constexpr DXGI_FORMAT kVelocityFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kDensityFormat = DXGI_FORMAT_R16_FLOAT;
constexpr DXGI_FORMAT kPressureFormat = DXGI_FORMAT_R32_FLOAT;
constexpr D3D12_RESOURCE_STATES kShaderRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

// 根の引数。HIKARI_FluidShaderContract.h の根の正準形と同じ並び。
enum Root : UINT {
    kDomainConstants,
    kPassConstants,
    kInput0,
    kInput1,
    kInput2,
    kInput3,
    kSources,
    kOutput0,
    kOutput1,
    kSliceOutput,
    kStats,
    kInteractions,
    kRootCount
};

constexpr const char* kEntryNames[] = {
    "ClearCS",      "AdvectForwardCS", "AdvectCorrectCS",
    "CurlCS",       "ForceFieldCS",    "ApplyForcesCS",
    "DivergenceCS", "SmoothCS",        "ResidualRestrictCS",
    "ProlongCS",    "CoarsestSolveCS", "ProjectCS",
    "OccupancyCS",  "DiagnosticsCS",   "SliceCS",
};
static_assert(std::size(kEntryNames) == static_cast<size_t>(FluidEntry::Count));

bool CreateRoot(ID3D12Device* device, ID3D12RootSignature** output) {
    std::array<CD3DX12_DESCRIPTOR_RANGE, 7> ranges{};
    std::array<CD3DX12_ROOT_PARAMETER, kRootCount> parameters{};
    parameters[kDomainConstants].InitAsConstantBufferView(0);
    parameters[kPassConstants].InitAsConstants(8, 1);
    for (UINT index = 0; index < 4; ++index) {
        ranges[index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, index);
        parameters[kInput0 + index].InitAsDescriptorTable(1, &ranges[index]);
    }
    parameters[kSources].InitAsShaderResourceView(6);
    ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
    ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4);
    parameters[kOutput0].InitAsDescriptorTable(1, &ranges[4]);
    parameters[kOutput1].InitAsDescriptorTable(1, &ranges[5]);
    parameters[kSliceOutput].InitAsDescriptorTable(1, &ranges[6]);
    parameters[kStats].InitAsUnorderedAccessView(5);
    parameters[kInteractions].InitAsShaderResourceView(7);
    std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> samplers{};
    samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                     D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    samplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                     D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, 0.0f, 16,
                     D3D12_COMPARISON_FUNC_ALWAYS, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK);
    const CD3DX12_ROOT_SIGNATURE_DESC description(kRootCount, parameters.data(), static_cast<UINT>(samplers.size()),
                                                  samplers.data());
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(device, description, output), "FluidSolver::CreateRoot");
}

// slice が偽なら 3D の体積、真なら調試の切片（2D、depth は 1）。
bool CreateVolume(ID3D12Device* device, DXGI_FORMAT format, uint32_t width, uint32_t height, uint32_t depth,
                  FluidVolume& volume, bool slice) {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = slice ? D3D12_RESOURCE_DIMENSION_TEXTURE2D : D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = static_cast<UINT16>(depth);
    description.MipLevels = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                             device, &heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                             nullptr, GFX::VIDEO_MEMORY::Owner::Vfx, IID_PPV_ARGS(volume.resource.GetAddressOf())),
                         "FluidSolver::CreateVolume"))
        return false;
    volume.srv = slice ? AllocateTexture2DSrvDescriptor(volume.resource.Get(), format)
                       : AllocateTexture3DSrvDescriptor(volume.resource.Get(), format);
    volume.uav = slice ? AllocateTexture2DUavDescriptor(volume.resource.Get(), format)
                       : AllocateTexture3DUavDescriptor(volume.resource.Get(), format, 0, 0, depth);
    volume.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    volume.size[0] = width;
    volume.size[1] = height;
    volume.size[2] = depth;
    return volume.srv.IsValid() && volume.uav.IsValid();
}

bool CreateBuffer(ID3D12Device* device, D3D12_HEAP_TYPE heapType, uint32_t bytes, D3D12_RESOURCE_STATES state,
                  ComPtr<ID3D12Resource>& buffer) {
    const CD3DX12_HEAP_PROPERTIES heap(heapType);
    const auto description = CD3DX12_RESOURCE_DESC::Buffer(bytes, heapType == D3D12_HEAP_TYPE_DEFAULT
                                                                      ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                                                      : D3D12_RESOURCE_FLAG_NONE);
    return HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &description,
                                                                      state, nullptr, GFX::VIDEO_MEMORY::Owner::Vfx,
                                                                      IID_PPV_ARGS(buffer.ReleaseAndGetAddressOf())),
                           "FluidSolver::CreateBuffer");
}

} // namespace

// 回転と倍率の逆で、域の局所の点を源の局所へ、源の局所の向きを域の局所へ移す行と、形・注入・力場を詰める。
FluidSourceGpu PackFluidSource(const FluidSourceSubmission& source, const MATH::Vec3& domainPosition,
                               const MATH::Quat& domainRotation, float cellSize, float burstDensity) {
    FluidSourceGpu gpu{};
    const MATH::Vec3 inverseScale{1.0f / (std::max)(std::abs(source.worldScale.x), 1.0e-4f),
                                  1.0f / (std::max)(std::abs(source.worldScale.y), 1.0e-4f),
                                  1.0f / (std::max)(std::abs(source.worldScale.z), 1.0e-4f)};
    const auto scaled = [&](const MATH::Vec3& v) {
        return MATH::Vec3{v.x * inverseScale.x, v.y * inverseScale.y, v.z * inverseScale.z};
    };
    const MATH::Vec3 basis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    MATH::Vec3 columns[3];
    MATH::Vec3 back[3];
    for (int axis = 0; axis < 3; ++axis) {
        columns[axis] =
            scaled(RotateToFluidLocal(source.worldRotation, MATH::RotateVector(domainRotation, basis[axis])));
        back[axis] = RotateToFluidLocal(domainRotation, MATH::RotateVector(source.worldRotation, basis[axis]));
    }
    const MATH::Vec3 translation =
        scaled(RotateToFluidLocal(source.worldRotation, domainPosition - source.worldPosition));
    const float* t = &translation.x;
    for (int row = 0; row < 3; ++row) {
        gpu.rows[row][0] = (&columns[0].x)[row];
        gpu.rows[row][1] = (&columns[1].x)[row];
        gpu.rows[row][2] = (&columns[2].x)[row];
        gpu.rows[row][3] = t[row];
        gpu.rows[3 + row][0] = (&back[0].x)[row];
        gpu.rows[3 + row][1] = (&back[1].x)[row];
        gpu.rows[3 + row][2] = (&back[2].x)[row];
    }
    const float averageScale = (inverseScale.x + inverseScale.y + inverseScale.z) / 3.0f;
    const float softEdge = 1.5f * cellSize * averageScale;
    const auto set = [&](int row, float x, float y, float z, float w) {
        gpu.rows[row][0] = x;
        gpu.rows[row][1] = y;
        gpu.rows[row][2] = z;
        gpu.rows[row][3] = w;
    };
    set(6, static_cast<float>(source.shape), source.radius, source.innerRadius, source.height);
    set(7, source.boxHalfExtents.x, source.boxHalfExtents.y, source.boxHalfExtents.z, source.coneAngleRadians);
    set(8, (std::max)(source.ratePerSecond, 0.0f), burstDensity, source.settings.temperature,
        source.settings.absorption);
    set(9, source.settings.velocity.x, source.settings.velocity.y, source.settings.velocity.z,
        source.settings.radialSpeed);
    const auto& field = source.field;
    set(10, field.center.x, field.center.y, field.center.z, field.radius);
    set(11, field.axis.x, field.axis.y, field.axis.z, field.vortex);
    set(12, field.turbulence, field.frequency, field.evolution, field.attraction);
    const MATH::Vec3 wind = RotateToFluidLocal(domainRotation, source.wind);
    set(13, wind.x, wind.y, wind.z, (std::max)(source.drag, 0.0f));
    set(14, source.settings.guideStrength, source.settings.guideThickness,
        source.settings.forceMode == FluidForceMode::TargetVelocity ? 1.0f : 0.0f, softEdge);
    return gpu;
}

FluidInteractionGpu PackFluidInteraction(const FluidInteraction& interaction, const MATH::Vec3& domainPosition,
                                         const MATH::Quat& domainRotation, const MATH::Vec3& domainVelocity) {
    FluidInteractionGpu gpu{};
    const MATH::Vec3 basis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    MATH::Vec3 columns[3];
    for (int axis = 0; axis < 3; ++axis)
        columns[axis] = RotateToFluidLocal(interaction.worldRotation, MATH::RotateVector(domainRotation, basis[axis]));
    const MATH::Vec3 translation =
        RotateToFluidLocal(interaction.worldRotation, domainPosition - interaction.worldPosition);
    for (int row = 0; row < 3; ++row) {
        gpu.rows[row][0] = (&columns[0].x)[row];
        gpu.rows[row][1] = (&columns[1].x)[row];
        gpu.rows[row][2] = (&columns[2].x)[row];
        gpu.rows[row][3] = (&translation.x)[row];
    }
    const auto set = [&](int row, const MATH::Vec3& v, float w) {
        gpu.rows[row][0] = v.x;
        gpu.rows[row][1] = v.y;
        gpu.rows[row][2] = v.z;
        gpu.rows[row][3] = w;
    };
    const bool obstacle = interaction.kind == FluidInteractionKind::Obstacle;
    const float directionLength = MATH::Length(interaction.direction);
    const MATH::Vec3 motion = obstacle                 ? interaction.velocity - domainVelocity
                              : directionLength > 0.0f ? interaction.direction * (1.0f / directionLength)
                                                       : MATH::Vec3{};
    set(3, interaction.extents, static_cast<float>(interaction.kind));
    set(4, RotateToFluidLocal(domainRotation, motion), static_cast<float>(interaction.shape));
    set(5, RotateToFluidLocal(domainRotation, interaction.worldPosition - domainPosition), interaction.strength);
    return gpu;
}

bool CreateFluidVolumeTexture(ID3D12Device* device, DXGI_FORMAT format, uint32_t width, uint32_t height, uint32_t depth,
                              FluidVolume& volume) {
    return CreateVolume(device, format, width, height, depth, volume, false);
}

bool CreateFluidSliceTexture(ID3D12Device* device, uint32_t width, uint32_t height, FluidVolume& volume) {
    return CreateVolume(device, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1u, volume, true);
}

void RetireVolume(const GFX::Context& context, FluidVolume& volume) {
    RetireRenderResourceDescriptor(volume.srv, context, "FluidSolver.VolumeSrv");
    RetireRenderResourceDescriptor(volume.uav, context, "FluidSolver.VolumeUav");
    if (volume.resource)
        GFX::RetireD3D12ObjectForFrame(volume.resource, context, "FluidSolver.Volume");
    volume = {};
}

bool FluidSolverPipeline::Ensure(ID3D12Device* device) {
    if (device == nullptr)
        return false;
    if (device_ == device && (failed_ || pipelines_.back()))
        return !failed_;
    device_ = device;
    failed_ = true;
    if (!CreateRoot(device, root_.ReleaseAndGetAddressOf()))
        return false;
    const GFX::ShaderDeviceCapabilities deviceCaps = GFX::QueryShaderDeviceCapabilities(device);
    const ASSETS::SHADER::ShaderRuntimeDeviceCapabilities caps{deviceCaps.shaderModel, deviceCaps.rootSignatureVersion,
                                                               deviceCaps.featureMask};
    for (size_t index = 0; index < pipelines_.size(); ++index) {
        auto resolved = ASSETS::SHADER::ResolveFluidSimulationShaderEntry(kEntryNames[index], caps);
        if (!resolved) {
            HIKARI_LOG_WARN(std::string("[FluidSmoke] Simulation shader unavailable: ") + kEntryNames[index] + ": " +
                            resolved.diagnostic);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
        description.pRootSignature = root_.Get();
        description.CS = {resolved.lease.GetDxil().data(), resolved.lease.GetDxil().size()};
        if (!HIKARI_DX_CHECK(device->CreateComputePipelineState(
                                 &description, IID_PPV_ARGS(pipelines_[index].ReleaseAndGetAddressOf())),
                             "FluidSolver::CreatePipeline"))
            return false;
    }
    failed_ = false;
    return true;
}

void FluidSolverPipeline::Retire(const GFX::Context& context) {
    for (auto& pipeline : pipelines_)
        if (pipeline)
            GFX::RetireD3D12ObjectForFrame(pipeline, context, "FluidSolver.Pipeline");
    if (root_)
        GFX::RetireD3D12ObjectForFrame(root_, context, "FluidSolver.Root");
    device_ = nullptr;
    failed_ = false;
}

bool CreateFluidDomainResources(ID3D12Device* device, const FluidGridLayout& layout, FluidDomainResources& resources) {
    resources = {};
    resources.layout = layout;
    const uint32_t* n = layout.cells;
    bool created = true;
    for (FluidVolume& volume : resources.velocity)
        created = created && CreateVolume(device, kVelocityFormat, n[0] + 1u, n[1] + 1u, n[2] + 1u, volume, false);
    for (FluidVolume& volume : resources.density)
        created = created && CreateVolume(device, kDensityFormat, n[0], n[1], n[2], volume, false);
    resources.pressure.resize(layout.levels);
    resources.rhs.resize(layout.levels);
    for (uint32_t level = 0u; level < layout.levels; ++level) {
        const uint32_t w = (std::max)(n[0] >> level, 1u), h = (std::max)(n[1] >> level, 1u),
                       d = (std::max)(n[2] >> level, 1u);
        created = created && CreateVolume(device, kPressureFormat, w, h, d, resources.pressure[level], false) &&
                  CreateVolume(device, kPressureFormat, w, h, d, resources.rhs[level], false);
    }
    created = created && CreateVolume(device, DXGI_FORMAT_R16G16_FLOAT, (n[0] + 1u) / 2u, (n[1] + 1u) / 2u,
                                      (n[2] + 1u) / 2u, resources.light, false);
    resources.bricks = ((n[0] + 7u) / 8u) * ((n[1] + 7u) / 8u) * ((n[2] + 7u) / 8u);
    resources.statsBytes = (resources.bricks * 12u + 255u) & ~255u;
    created = created && CreateBuffer(device, D3D12_HEAP_TYPE_DEFAULT, resources.statsBytes,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, resources.stats);
    for (auto& readback : resources.readback)
        created = created && CreateBuffer(device, D3D12_HEAP_TYPE_READBACK, resources.statsBytes,
                                          D3D12_RESOURCE_STATE_COPY_DEST, readback);
    resources.statsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    resources.bytes = EstimateFluidDomainBytes(layout) + resources.statsBytes;
    return created;
}

void RetireFluidDomainResources(const GFX::Context& context, FluidDomainResources& resources) {
    for (FluidVolume& volume : resources.velocity)
        RetireVolume(context, volume);
    for (FluidVolume& volume : resources.density)
        RetireVolume(context, volume);
    for (FluidVolume& volume : resources.pressure)
        RetireVolume(context, volume);
    for (FluidVolume& volume : resources.rhs)
        RetireVolume(context, volume);
    RetireVolume(context, resources.light);
    if (resources.stats)
        GFX::RetireD3D12ObjectForFrame(resources.stats, context, "FluidSolver.Stats");
    for (auto& readback : resources.readback)
        if (readback)
            GFX::RetireD3D12ObjectForFrame(readback, context, "FluidSolver.Readback");
    resources = {};
}

FluidRecorder::FluidRecorder(ID3D12GraphicsCommandList* commands, const FluidSolverPipeline& pipeline,
                             FluidDomainResources& domain, D3D12_GPU_VIRTUAL_ADDRESS constants) noexcept
    : commands_(commands), pipeline_(pipeline), domain_(domain), constants_(constants) {
    commands_->SetComputeRootSignature(pipeline_.Root());
}

void FluidRecorder::Transition(FluidVolume& volume, D3D12_RESOURCE_STATES state) {
    if (volume.state == state)
        return;
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(volume.resource.Get(), volume.state, state);
    commands_->ResourceBarrier(1, &barrier);
    volume.state = state;
}

void FluidRecorder::Dispatch(FluidEntry entry, const FluidPassBindings& bindings, const uint32_t (&passSize)[3],
                             const uint32_t (&passParams)[4], const uint32_t (&groups)[3]) {
    for (FluidVolume* input : bindings.inputs)
        if (input != nullptr)
            Transition(*input, kShaderRead);
    for (FluidVolume* output : bindings.outputs)
        if (output != nullptr)
            Transition(*output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (bindings.slice != nullptr)
        Transition(*bindings.slice, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (bindings.stats && domain_.statsState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(domain_.stats.Get(), domain_.statsState,
                                                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands_->ResourceBarrier(1, &barrier);
        domain_.statsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    commands_->SetPipelineState(pipeline_.Get(entry));
    commands_->SetComputeRootConstantBufferView(kDomainConstants, constants_);
    const uint32_t constants[8] = {passSize[0],   passSize[1],   passSize[2],   0u,
                                   passParams[0], passParams[1], passParams[2], passParams[3]};
    commands_->SetComputeRoot32BitConstants(kPassConstants, 8, constants, 0);
    for (UINT index = 0; index < bindings.inputs.size(); ++index)
        if (bindings.inputs[index] != nullptr)
            commands_->SetComputeRootDescriptorTable(kInput0 + index, bindings.inputs[index]->srv.gpu);
    if (bindings.sources != 0u)
        commands_->SetComputeRootShaderResourceView(kSources, bindings.sources);
    if (bindings.interactions != 0u)
        commands_->SetComputeRootShaderResourceView(kInteractions, bindings.interactions);
    if (bindings.outputs[0] != nullptr)
        commands_->SetComputeRootDescriptorTable(kOutput0, bindings.outputs[0]->uav.gpu);
    if (bindings.outputs[1] != nullptr)
        commands_->SetComputeRootDescriptorTable(kOutput1, bindings.outputs[1]->uav.gpu);
    if (bindings.slice != nullptr)
        commands_->SetComputeRootDescriptorTable(kSliceOutput, bindings.slice->uav.gpu);
    if (bindings.stats)
        commands_->SetComputeRootUnorderedAccessView(kStats, domain_.stats->GetGPUVirtualAddress());
    commands_->Dispatch((std::max)(groups[0], 1u), (std::max)(groups[1], 1u), (std::max)(groups[2], 1u));
    const auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    commands_->ResourceBarrier(1, &uavBarrier);
}

void FluidRecorder::Clear(FluidVolume& volume) {
    FluidPassBindings bindings{};
    bindings.outputs[0] = &volume;
    const uint32_t size[3] = {volume.size[0], volume.size[1], volume.size[2]};
    Dispatch(FluidEntry::Clear, bindings, size, {0u, 0u, 0u, 0u},
             {Groups(size[0], 4u), Groups(size[1], 4u), Groups(size[2], 4u)});
}

void FluidRecorder::Substep(uint32_t& velocityIndex, uint32_t& densityIndex, D3D12_GPU_VIRTUAL_ADDRESS sources,
                            D3D12_GPU_VIRTUAL_ADDRESS interactions, uint32_t vCycles) {
    auto& v = domain_.velocity;
    auto& d = domain_.density;
    const uint32_t a = velocityIndex, b = (a + 1u) % 3u, c = (a + 2u) % 3u;
    const uint32_t e = densityIndex, f = (e + 1u) % 3u, g = (e + 2u) % 3u;
    const uint32_t* n = domain_.layout.cells;
    const uint32_t cells[3] = {n[0], n[1], n[2]};
    const uint32_t faces[3] = {n[0] + 1u, n[1] + 1u, n[2] + 1u};
    const uint32_t cellGroups[3] = {Groups(n[0], 4u), Groups(n[1], 4u), Groups(n[2], 4u)};
    const uint32_t faceGroups[3] = {Groups(faces[0], 4u), Groups(faces[1], 4u), Groups(faces[2], 4u)};
    const uint32_t none[4] = {};

    Dispatch(FluidEntry::AdvectForward, {{&v[a], &d[e]}, {&v[b], &d[f]}}, faces, none, faceGroups);
    Dispatch(FluidEntry::AdvectCorrect, {{&v[a], &d[e], &v[b], &d[f]}, {&v[c], &d[g]}}, faces, none, faceGroups);
    Dispatch(FluidEntry::Curl, {{&v[c]}, {&v[b]}}, cells, none, cellGroups);
    FluidPassBindings forces{{&v[c], &v[b]}, {&v[a], &d[g]}};
    forces.sources = sources;
    forces.interactions = interactions;
    Dispatch(FluidEntry::ForceField, forces, cells, none, cellGroups);
    Dispatch(FluidEntry::ApplyForces, {{&v[c], &v[a]}, {&v[b]}}, faces, none, faceGroups);
    FluidPassBindings divergence{{&v[b]}, {&domain_.rhs[0]}};
    divergence.interactions = interactions;
    Dispatch(FluidEntry::Divergence, divergence, cells, none, cellGroups);
    for (uint32_t cycle = 0u; cycle < vCycles; ++cycle)
        VCycle();
    Dispatch(FluidEntry::Project, {{&domain_.pressure[0]}, {&v[b]}}, faces, none, faceGroups);
    velocityIndex = b;
    densityIndex = g;
}

void FluidRecorder::VCycle() {
    const uint32_t levels = domain_.layout.levels;
    const auto smooth = [&](uint32_t level) {
        const FluidVolume& p = domain_.pressure[level];
        const uint32_t size[3] = {p.size[0], p.size[1], p.size[2]};
        const uint32_t groups[3] = {Groups(size[0], 4u), Groups(size[1], 4u), Groups(size[2], 4u)};
        for (uint32_t color = 0u; color < 2u; ++color)
            Dispatch(FluidEntry::Smooth, {{&domain_.rhs[level]}, {&domain_.pressure[level]}}, size, {color, 0u, 0u, 0u},
                     groups);
    };
    for (uint32_t level = 0u; level + 1u < levels; ++level) {
        smooth(level);
        const FluidVolume& coarse = domain_.pressure[level + 1u];
        const uint32_t size[3] = {coarse.size[0], coarse.size[1], coarse.size[2]};
        Dispatch(FluidEntry::ResidualRestrict,
                 {{&domain_.pressure[level], &domain_.rhs[level]},
                  {&domain_.rhs[level + 1u], &domain_.pressure[level + 1u]}},
                 size, {0u, 0u, 0u, 0u}, {Groups(size[0], 4u), Groups(size[1], 4u), Groups(size[2], 4u)});
    }
    const FluidVolume& coarsest = domain_.pressure[levels - 1u];
    const uint32_t coarsestSize[3] = {coarsest.size[0], coarsest.size[1], coarsest.size[2]};
    Dispatch(FluidEntry::CoarsestSolve, {{&domain_.rhs[levels - 1u]}, {&domain_.pressure[levels - 1u]}}, coarsestSize,
             {0u, 0u, 0u, 0u}, {1u, 1u, 1u});
    for (uint32_t level = levels - 1u; level-- > 0u;) {
        const FluidVolume& fine = domain_.pressure[level];
        const uint32_t size[3] = {fine.size[0], fine.size[1], fine.size[2]};
        Dispatch(FluidEntry::Prolong, {{&domain_.pressure[level + 1u]}, {&domain_.pressure[level]}}, size,
                 {0u, 0u, 0u, 0u}, {Groups(size[0], 4u), Groups(size[1], 4u), Groups(size[2], 4u)});
        smooth(level);
    }
}

void FluidRecorder::Occupancy(uint32_t densityIndex) {
    FluidPassBindings bindings{{&domain_.density[densityIndex]}, {}};
    bindings.stats = true;
    const uint32_t* n = domain_.layout.cells;
    const uint32_t size[3] = {n[0], n[1], n[2]};
    Dispatch(FluidEntry::Occupancy, bindings, size, {0u, 0u, 0u, 0u},
             {Groups(n[0], 8u), Groups(n[1], 8u), Groups(n[2], 8u)});
}

void FluidRecorder::Diagnostics(uint32_t velocityIndex, uint32_t densityIndex) {
    FluidPassBindings bindings{{&domain_.velocity[velocityIndex], &domain_.density[densityIndex]}, {}};
    bindings.stats = true;
    const uint32_t* n = domain_.layout.cells;
    const uint32_t size[3] = {n[0], n[1], n[2]};
    Dispatch(FluidEntry::Diagnostics, bindings, size, {0u, 0u, 0u, domain_.DiagnosticsOffset()},
             {Groups(n[0], 8u), Groups(n[1], 8u), Groups(n[2], 8u)});
}

void FluidRecorder::Slice(uint32_t velocityIndex, uint32_t densityIndex, FluidVolume& output, FluidDebugField field,
                          uint32_t axis, uint32_t index, bool visualize) {
    FluidPassBindings bindings{{&domain_.velocity[velocityIndex], &domain_.density[densityIndex], &domain_.pressure[0]},
                               {}};
    bindings.slice = &output;
    const uint32_t size[3] = {output.size[0], output.size[1], 1u};
    Dispatch(FluidEntry::Slice, bindings, size, {visualize ? 1u : 0u, static_cast<uint32_t>(field), axis, index},
             {Groups(size[0], 8u), Groups(size[1], 8u), 1u});
}

void FluidRecorder::CopyStats(ID3D12Resource* readback) {
    const auto toCopy =
        CD3DX12_RESOURCE_BARRIER::Transition(domain_.stats.Get(), domain_.statsState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands_->ResourceBarrier(1, &toCopy);
    commands_->CopyBufferRegion(readback, 0, domain_.stats.Get(), 0, domain_.statsBytes);
    domain_.statsState = D3D12_RESOURCE_STATE_COPY_SOURCE;
}

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
