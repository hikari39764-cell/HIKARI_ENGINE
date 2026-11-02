#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingScene.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct GpuSceneSurfaceRecord;
}
namespace HIKARI::RENDER3D::MATERIAL {
struct GpuMaterialData;
}

// 光追の場面へ表面を入れるかどうかと、入れ方を決める入力。
namespace HIKARI::RENDER3D::RAYTRACING {

// 頂点が GPU で変形する面などは今は入れられない。入れられる面は nullopt（半透明は透過する面として入れる）。
std::optional<RuntimeRayTracingExclusion> ClassifyRayExclusion(const GPUDRIVEN::GpuSceneSurfaceRecord& record);
bool IsValidRayTransform(const MATH::Mat4& world);

// 表面を加速構造へどう入れるかを決める入力（world 行列と材質を除く）。前回と全部同じなら、変わったのは配置か
// 材質だけ。材質はプログラム（画面や微分を使うと外す）だけが入れ方を変え、slot は hit 記録の番号を直せば足りる。
struct RayRecordInputs {
    uint64_t object = 0, lifetime = 0;
    uint32_t geometryIndex = 0, geometryGeneration = 0;
    uint32_t node = 0, mesh = 0, primitive = 0;
    uint32_t materialProgram = 0;
    uint32_t terrainIndex = 0;
    uint32_t bits = 0;
    friend bool operator==(const RayRecordInputs&, const RayRecordInputs&) = default;
};
RayRecordInputs ReadRayRecordInputs(const GPUDRIVEN::GpuSceneSurfaceRecord& record, size_t index,
                                    std::span<const uint32_t> materialSlots,
                                    std::span<const MATERIAL::GpuMaterialData> materials);

} // namespace HIKARI::RENDER3D::RAYTRACING
