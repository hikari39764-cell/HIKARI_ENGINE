#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingMass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <d3dx12.h>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/GpuDriven/HIKARI_MassGpuScene.h"

namespace HIKARI::RENDER3D::RAYTRACING {
namespace {
constexpr const wchar_t* kShaderPath = L"HIKARI/Shaders/Render3D_RayTracingMassInstancesCS.hlsl";
constexpr uint32_t kThreadsPerGroup = 64u;

// 描画（HikariBuildMassMatrices）と同じ式で、位置・四元数・拡縮を instance の 3×4 へ書く。値が有限でなければ隠す。
void WriteTransform(D3D12_RAYTRACING_INSTANCE_DESC& instance, const RUNTIME::MassRenderProxyGpuRecord& record) {
    RUNTIME::MassRenderFloat4 q = record.rotation;
    const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (MATH::IsFinite(lengthSquared) && lengthSquared > 1.0e-12f) {
        const float inverse = 1.0f / std::sqrt(lengthSquared);
        q = {q.x * inverse, q.y * inverse, q.z * inverse, q.w * inverse};
    } else {
        q = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    const float rotation[3][3] = {{1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy)},
                                  {2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx)},
                                  {2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy)}};
    const float scale[3] = {record.scale.x, record.scale.y, record.scale.z};
    const float position[3] = {record.position.x, record.position.y, record.position.z};
    bool finite = true;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col)
            instance.Transform[row][col] = rotation[row][col] * scale[col];
        instance.Transform[row][3] = position[row];
        for (int col = 0; col < 4; ++col)
            finite = finite && MATH::IsFinite(instance.Transform[row][col]);
    }
    instance.InstanceMask = finite ? 0xff : 0x00;
    if (!finite)
        std::memset(instance.Transform, 0, sizeof(instance.Transform));
}
} // namespace

void RuntimeRayTracingMass::Begin(const GPUDRIVEN::MassGpuSceneSource* mass, std::span<const uint32_t> materialSlots) {
    source_ = mass;
    present_ = mass && mass->HasInstances();
    worldIdentity_ = mass ? mass->worldIdentity : 0;
    layoutVersion_ = mass ? mass->layoutVersion : 0;
    sourceVersion_ = mass ? mass->sourceVersion : 0;
    materialSlots_.assign(materialSlots.begin(), materialSlots.end());
    Drop();
}

void RuntimeRayTracingMass::Drop() {
    stores_.clear();
    chunks_.clear();
    firstInstance_ = 0;
    tableDirty_ = true;
}

bool RuntimeRayTracingMass::Append(const GPUDRIVEN::MassGpuSceneSource& mass, std::span<const Shape> shapes,
                                   uint32_t maximumInstances, std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instances) {
    Drop();
    stores_.assign(shapes.size(), {});
    uint64_t total = instances.size();
    for (size_t index = 0; index < mass.instances.size(); ++index) {
        const uint32_t store = mass.instances[index].sourceRecordIndex;
        if (store >= shapes.size()) {
            Drop();
            return false;
        }
        total += shapes[store].size();
    }
    if (total > maximumInstances) {
        Drop();
        return false;
    }
    firstInstance_ = static_cast<uint32_t>(instances.size());
    // 個体は種類ごとに続いて並ぶ（MassGpuSceneRegistry の並び）。
    for (size_t index = 0; index < mass.instances.size(); ++index) {
        const auto& record = mass.instances[index];
        const Shape& shape = shapes[record.sourceRecordIndex];
        Store& store = stores_[record.sourceRecordIndex];
        if (store.recordCount == 0) {
            store.firstRecord = static_cast<uint32_t>(index);
            store.firstInstance = static_cast<uint32_t>(instances.size());
            store.chunks = static_cast<uint32_t>(shape.size());
            store.firstChunk = static_cast<uint32_t>(chunks_.size());
            for (const auto& [bottomLevel, firstHit] : shape)
                chunks_.push_back({bottomLevel, firstHit});
        } else if (store.firstRecord + store.recordCount != index) {
            Drop();
            return false;
        }
        ++store.recordCount;
        for (const auto& [bottomLevel, firstHit] : shape) {
            D3D12_RAYTRACING_INSTANCE_DESC instance{};
            WriteTransform(instance, record);
            instance.InstanceID = firstHit;
            instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            instance.AccelerationStructure = bottomLevel;
            instances.push_back(instance);
        }
    }
    return true;
}

bool RuntimeRayTracingMass::Matches(const GPUDRIVEN::MassGpuSceneSource* mass,
                                    std::span<const uint32_t> materialSlots) const {
    if (!mass || !mass->HasInstances())
        return !present_;
    return present_ && mass == source_ && mass->worldIdentity == worldIdentity_ &&
           mass->layoutVersion == layoutVersion_ && materialSlots.size() == materialSlots_.size() &&
           std::equal(materialSlots.begin(), materialSlots.end(), materialSlots_.begin());
}

bool RuntimeRayTracingMass::Moved(const GPUDRIVEN::MassGpuSceneSource* mass) const {
    return (mass ? mass->sourceVersion : 0) != sourceVersion_;
}

std::optional<bool> RuntimeRayTracingMass::Update(const GPUDRIVEN::MassGpuSceneSource* mass,
                                                  std::span<D3D12_RAYTRACING_INSTANCE_DESC> instances) {
    if (!Moved(mass))
        return false;
    // 入れなかった大量配置は動いても組み直さない（並びの版だけ進める）。
    if (stores_.empty()) {
        sourceVersion_ = mass ? mass->sourceVersion : 0;
        return false;
    }
    if (!mass->CollectDirtyRangesSince(sourceVersion_, dirty_))
        dirty_.assign(1, {0u, static_cast<uint32_t>(mass->instances.size())});
    for (const auto& range : dirty_) {
        for (uint32_t index = range.firstInstance; index - range.firstInstance < range.instanceCount; ++index) {
            if (index >= mass->instances.size())
                return std::nullopt;
            const auto& record = mass->instances[index];
            if (record.sourceRecordIndex >= stores_.size())
                return std::nullopt;
            const Store& store = stores_[record.sourceRecordIndex];
            if (index < store.firstRecord || index - store.firstRecord >= store.recordCount)
                return std::nullopt;
            const size_t first = store.firstInstance + size_t(index - store.firstRecord) * store.chunks;
            if (first + store.chunks > instances.size())
                return std::nullopt;
            for (uint32_t chunk = 0; chunk < store.chunks; ++chunk)
                WriteTransform(instances[first + chunk], record);
        }
    }
    sourceVersion_ = mass->sourceVersion;
    return true;
}

RuntimeRayTracingMass::~RuntimeRayTracingMass() { Reset(GFX::GetCurrentContext()); }

bool RuntimeRayTracingMass::EnsurePipeline(const GFX::Context& graphics) {
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
    std::array<CD3DX12_ROOT_PARAMETER, 5> parameters;
    parameters[0].InitAsConstants(4, 0);
    parameters[1].InitAsShaderResourceView(0, 8); // 描画の常駐記録（gMassRenderProxyBuffer）
    parameters[2].InitAsShaderResourceView(1);
    parameters[3].InitAsShaderResourceView(2);
    parameters[4].InitAsUnorderedAccessView(0);
    const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(parameters.size()), parameters.data());
    if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(graphics.device, rootDesc, root_.GetAddressOf()),
                         "RayTracingMassInstances root signature"))
        return false;
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    if (!GFX::CompileShaderFileSm6(kShaderPath, "CSMain", L"cs_6_5", shader.GetAddressOf())) {
        DEBUGLOG::PushRenderError(
            "[RT][Mass] compute shader compile failed; moving mass instances are written on CPU.");
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root_.Get();
    desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
    if (!HIKARI_DX_CHECK(graphics.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(pipeline_.GetAddressOf())),
                         "RayTracingMassInstances pipeline"))
        return false;
    pipelineFailed_ = false;
    return true;
}

bool RuntimeRayTracingMass::CanDispatch(const GFX::Context& graphics) { return EnsurePipeline(graphics); }

bool RuntimeRayTracingMass::Dispatch(const GFX::Context& graphics, const GPUDRIVEN::MassGpuSceneSource& mass,
                                     D3D12_GPU_VIRTUAL_ADDRESS records, D3D12_GPU_VIRTUAL_ADDRESS instances) {
    if (stores_.empty() || chunks_.empty() || records == 0 || instances == 0 || graphics.cmdList == nullptr ||
        &mass != source_ || mass.layoutVersion != layoutVersion_ || !EnsurePipeline(graphics))
        return false;
    if (tableDirty_ || !table_.Current()) {
        chunkOffset_ = stores_.size() * sizeof(Store);
        std::vector<std::byte> bytes(chunkOffset_ + chunks_.size() * sizeof(Chunk));
        std::memcpy(bytes.data(), stores_.data(), chunkOffset_);
        std::memcpy(bytes.data() + chunkOffset_, chunks_.data(), chunks_.size() * sizeof(Chunk));
        if (!table_.Write(graphics, bytes.data(), bytes.size(), "RT.Mass.Table"))
            return false;
        tableDirty_ = false;
    }
    table_.MarkRead(graphics);
    auto* commands = graphics.cmdList;
    GFX::PIX::ScopedGpuEvent event(commands, GFX::PIX::kColorRender, "RayTracing.MassInstances");
    const uint32_t constants[4]{static_cast<uint32_t>(mass.instances.size()), static_cast<uint32_t>(stores_.size()), 0u,
                                0u};
    const D3D12_GPU_VIRTUAL_ADDRESS table = table_.Current()->GetGPUVirtualAddress();
    commands->SetComputeRootSignature(root_.Get());
    commands->SetPipelineState(pipeline_.Get());
    commands->SetComputeRoot32BitConstants(0, 4, constants, 0);
    commands->SetComputeRootShaderResourceView(1, records);
    commands->SetComputeRootShaderResourceView(2, table);
    commands->SetComputeRootShaderResourceView(3, table + chunkOffset_);
    commands->SetComputeRootUnorderedAccessView(4, instances);
    commands->Dispatch((constants[0] + kThreadsPerGroup - 1) / kThreadsPerGroup, 1, 1);
    sourceVersion_ = mass.sourceVersion;
    return true;
}

void RuntimeRayTracingMass::Reset(const GFX::Context& graphics) {
    source_ = nullptr;
    present_ = false;
    worldIdentity_ = layoutVersion_ = sourceVersion_ = 0;
    materialSlots_.clear();
    Drop();
    dirty_.clear();
    table_.Reset(graphics, "RT.Mass.Table");
}
} // namespace HIKARI::RENDER3D::RAYTRACING
