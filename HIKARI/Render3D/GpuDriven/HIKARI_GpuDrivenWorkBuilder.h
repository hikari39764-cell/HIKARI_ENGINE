#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d12.h>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenProducer.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenWorkContext {
        IGpuDrivenProducer* producer = nullptr;
        ID3D12GraphicsCommandList* commandList = nullptr;
        MATH::Mat4 viewProj{};
        MATH::Vec3 cameraPosition{};
        D3D12_GPU_DESCRIPTOR_HANDLE geometryPoolSrv{};
        D3D12_GPU_VIRTUAL_ADDRESS surfaceGpuSceneGpuAddress = 0;
        const GpuDrivenFrame* frame = nullptr;
        uint32_t passMask = 0xffffffffu;
        bool collectCounterReadback = true;
        GpuDrivenDepthOcclusionContext depthOcclusion{};
    };

    struct GpuDrivenWorkResult {
        bool submitted = false;
        size_t sourcePassCount = 0;
        size_t sourceInstanceCount = 0;
    };

    GpuDrivenWorkResult BuildGpuDrivenWork(
        const GpuDrivenWorkContext& context);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
