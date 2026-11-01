#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingChanges.h"

#include <algorithm>
#include <cstring>
#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"

namespace HIKARI::RENDER3D::RAYTRACING {
void RuntimeRayTracingChanges::Reset() {
    previous_.clear();
    minimum = maximum = {0, 0, 0, 0};
}

void RuntimeRayTracingChanges::Update(std::span<const GPUDRIVEN::GpuSceneSurfaceRecord> records) {
    minimum = maximum = {0, 0, 0, 0};
    const auto include = [this](const Bounds& bounds) {
        if (!MATH::IsFinite(bounds.min) || !MATH::IsFinite(bounds.max))
            return;
        if (minimum.w == 0) {
            minimum = {bounds.min.x, bounds.min.y, bounds.min.z, 1};
            maximum = {bounds.max.x, bounds.max.y, bounds.max.z, 1};
        } else {
            minimum = {std::min(minimum.x, bounds.min.x), std::min(minimum.y, bounds.min.y),
                       std::min(minimum.z, bounds.min.z), 1};
            maximum = {std::max(maximum.x, bounds.max.x), std::max(maximum.y, bounds.max.y),
                       std::max(maximum.z, bounds.max.z), 1};
        }
    };
    for (size_t i = records.size(); i < previous_.size(); ++i)
        if (previous_[i].active)
            include(previous_[i].bounds);
    previous_.resize(records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        Surface next{record.objectId.value, record.objectLifetimeGeneration, record.clusterWorldMatrix,
                     record.worldBounds, record.valid && record.visible};
        const auto& old = previous_[i];
        // 骨格は world 行列が同じでも変わる。保守的にその範囲を毎フレーム更新する。
        if (old.active != next.active || old.object != next.object || old.lifetime != next.lifetime || record.skinned ||
            std::memcmp(&old.world, &next.world, sizeof(next.world)) != 0) {
            if (old.active)
                include(old.bounds);
            if (next.active)
                include(next.bounds);
        }
        previous_[i] = next;
    }
}
} // namespace HIKARI::RENDER3D::RAYTRACING
