#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Render3D/GpuDriven/HIKARI_GpuCommandBuildResult.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenPass.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneFrame.h"
#include "Render3D/GpuDriven/HIKARI_GpuVisibilityResult.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenDrawCommandRange;
    struct GpuDrivenTraditionalIndirectView;

    enum class GeometryBackendKind : uint32_t {
        GpuDrivenTraditionalVsPs,
        GpuDrivenMeshShader,
    };

    struct GeometryBackendContext {
        ID3D12GraphicsCommandList* commandList = nullptr;
        GpuDrivenPassKind requestedPass = GpuDrivenPassKind::ForwardOpaque;
        GpuDrivenPassKind pass = GpuDrivenPassKind::ForwardOpaque;
        GeometryBackendKind backend = GeometryBackendKind::GpuDrivenTraditionalVsPs;
        const GpuSceneFrame* scene = nullptr;
        const GpuVisibilityResult* visibility = nullptr;
        const GpuCommandBuildResult* commands = nullptr;
        const GpuDrivenDrawCommandRange* drawCommandRange = nullptr;
        const GpuDrivenTraditionalIndirectView* traditionalIndirect = nullptr;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
