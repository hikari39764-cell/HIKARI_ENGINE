#pragma once

#include "Render3D/GpuDriven/HIKARI_GpuDrivenPass.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenPassDesc {
        GpuDrivenPassKind pass = GpuDrivenPassKind::ForwardOpaque;
        bool needsMaterial = false;
        bool depthOnly = false;
        bool allowClusterMainline = false;
        bool allowMeshShader = false;
        bool allowTransparent = false;
        bool allowSkinned = false;
        bool allowMaterialFx = false;
        bool needsSorting = false;
    };

    const GpuDrivenPassDesc& GetGpuDrivenPassDesc(GpuDrivenPassKind pass);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
