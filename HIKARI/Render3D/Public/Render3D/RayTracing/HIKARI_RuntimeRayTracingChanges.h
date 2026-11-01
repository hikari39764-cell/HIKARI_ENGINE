#pragma once

#include <span>
#include <vector>
#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct GpuSceneSurfaceRecord;
}

namespace HIKARI::RENDER3D::RAYTRACING {
// GI の更新優先範囲。前後の形状を含み、物体が去った場所の遮蔽も更新する。
class RuntimeRayTracingChanges {
public:
    void Update(std::span<const GPUDRIVEN::GpuSceneSurfaceRecord> records);
    void Reset();
    MATH::Vec4 minimum{0, 0, 0, 0}; // w = 有効
    MATH::Vec4 maximum{0, 0, 0, 0};

private:
    struct Surface {
        uint64_t object = 0, lifetime = 0;
        MATH::Mat4 world{};
        Bounds bounds{};
        bool active = false;
    };
    std::vector<Surface> previous_;
};
} // namespace HIKARI::RENDER3D::RAYTRACING
