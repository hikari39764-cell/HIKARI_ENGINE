#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingMass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/GpuDriven/HIKARI_MassGpuScene.h"

namespace HIKARI::RENDER3D::RAYTRACING {
namespace {
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
    stores_.clear();
}

bool RuntimeRayTracingMass::Append(const GPUDRIVEN::MassGpuSceneSource& mass, std::span<const Shape> shapes,
                                   uint32_t maximumInstances, std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instances) {
    stores_.assign(shapes.size(), {});
    uint64_t total = instances.size();
    for (size_t index = 0; index < mass.instances.size(); ++index) {
        const uint32_t store = mass.instances[index].sourceRecordIndex;
        if (store >= shapes.size()) {
            stores_.clear();
            return false;
        }
        total += shapes[store].size();
    }
    if (total > maximumInstances) {
        stores_.clear();
        return false;
    }
    // 個体は種類ごとに続いて並ぶ（MassGpuSceneRegistry の並び）。
    for (size_t index = 0; index < mass.instances.size(); ++index) {
        const auto& record = mass.instances[index];
        const Shape& shape = shapes[record.sourceRecordIndex];
        Store& store = stores_[record.sourceRecordIndex];
        if (store.recordCount == 0) {
            store.firstRecord = static_cast<uint32_t>(index);
            store.firstInstance = static_cast<uint32_t>(instances.size());
            store.chunks = static_cast<uint32_t>(shape.size());
        } else if (store.firstRecord + store.recordCount != index) {
            stores_.clear();
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

void RuntimeRayTracingMass::Reset() {
    source_ = nullptr;
    present_ = false;
    worldIdentity_ = layoutVersion_ = sourceVersion_ = 0;
    materialSlots_.clear();
    stores_.clear();
    dirty_.clear();
}
} // namespace HIKARI::RENDER3D::RAYTRACING
