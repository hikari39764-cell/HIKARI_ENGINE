#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingInputs.h"

#include <bit>
#include <cmath>

#include <DirectXMath.h>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"

namespace HIKARI::RENDER3D::RAYTRACING {

// 頂点が GPU で変形する面などは今は入れられない。入れられる面は nullopt（半透明は透過する面として入れる）。
std::optional<RuntimeRayTracingExclusion> ClassifyRayExclusion(const GPUDRIVEN::GpuSceneSurfaceRecord& record) {
    using enum RuntimeRayTracingExclusion;
    if (record.waterSurface)
        return Water;
    if (record.skinned)
        return Skinned;
    if (record.foliageWind)
        return FoliageWind;
    if (record.terrainSurface)
        return Terrain;
    if (record.key.customVertexShader)
        return CustomVertex;
    if (record.key.materialFx)
        return MaterialEffect;
    return std::nullopt;
}
bool IsValidRayTransform(const MATH::Mat4& world) {
    if (!MATH::IsFinite(world) || world.m[0][3] != 0 || world.m[1][3] != 0 || world.m[2][3] != 0 || world.m[3][3] != 1)
        return false;
    const auto matrix = std::bit_cast<DirectX::XMFLOAT4X4>(world);
    const float determinant = DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&matrix)));
    return std::isfinite(determinant) && determinant != 0;
}
RayRecordInputs ReadRayRecordInputs(const GPUDRIVEN::GpuSceneSurfaceRecord& record, size_t index,
                                    std::span<const uint32_t> materialSlots,
                                    std::span<const MATERIAL::GpuMaterialData> materials) {
    const bool flags[] = {record.visible,
                          record.valid,
                          record.skinned,
                          record.foliageWind,
                          record.waterSurface,
                          record.terrainSurface,
                          record.key.customVertexShader,
                          record.key.materialFx,
                          record.jointPaletteMatrixCount != 0,
                          record.key.doubleSided,
                          record.castShadow,
                          record.key.transparent};
    uint32_t bits = 0;
    for (size_t b = 0; b < std::size(flags); ++b)
        bits |= flags[b] ? 1u << b : 0u;
    const auto handle = record.key.resources.clusterGeometry;
    const uint32_t slot = index < materialSlots.size() ? materialSlots[index] : 0xffffffffu;
    return {record.objectId.value,
            record.objectLifetimeGeneration,
            handle.index,
            handle.generation,
            record.nodeIndex,
            record.meshIndex,
            record.primitiveIndex,
            slot < materials.size() ? materials[slot].materialProgramIndex : 0xffffffffu,
            record.terrainSurfaceGpuDataIndex,
            bits};
}

} // namespace HIKARI::RENDER3D::RAYTRACING
