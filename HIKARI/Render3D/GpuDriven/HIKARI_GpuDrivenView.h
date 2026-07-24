#pragma once

#include <cstdint>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenPass.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenView {
        GpuDrivenPassKind pass = GpuDrivenPassKind::ForwardOpaque;
        MATH::Mat4 viewProj{};
        MATH::Vec3 viewPosition{};
        uint32_t passMask = MakeGpuDrivenPassMask(GpuDrivenPassKind::ForwardOpaque);
        uint32_t flags = 0;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
