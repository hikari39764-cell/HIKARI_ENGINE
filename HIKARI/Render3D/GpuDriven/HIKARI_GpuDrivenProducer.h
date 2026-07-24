#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d12.h>

#include "Render3D/GpuDriven/HIKARI_GpuCommandBuildResult.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrame.h"
#include "Render3D/GpuDriven/HIKARI_GpuVisibilityResult.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    enum class GpuDrivenProducerKind : uint32_t {
        None,
        ClusterGpuCulling,
    };

    struct GpuDrivenProducerFrameOutput {
        GpuDrivenProducerKind producerKind = GpuDrivenProducerKind::None;
        GpuVisibilityResult visibility{};
        GpuCommandBuildResult commands{};
        bool visibilityReady = false;
        bool commandBuildReady = false;
        size_t visibilitySeedCount = 0;
        size_t visibilityOverflowInstanceCount = 0;
    };

    struct GpuDrivenDepthOcclusionContext {
        bool enabled = false;
        D3D12_GPU_DESCRIPTOR_HANDLE hzbSrv{};
        uint32_t hzbWidth = 0;
        uint32_t hzbHeight = 0;
        uint32_t hzbMipCount = 0;
        MATH::Mat4 hzbViewProj{};
        bool hzbViewProjValid = false;
        HIKARI::RENDER3D::DEPTH::DepthPyramidView depthPyramid{};
    };

    struct GpuDrivenProducerWorkContext {
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

    struct GpuDrivenProducerWorkResult {
        bool submitted = false;
        size_t sourcePassCount = 0;
        size_t sourceInstanceCount = 0;
    };

    class IGpuDrivenProducer {
    public:
        virtual ~IGpuDrivenProducer() = default;

        virtual GpuDrivenProducerKind GetProducerKind() const = 0;
        virtual void BeginFrame(bool collectCounterReadback) = 0;
        virtual GpuDrivenProducerWorkResult DispatchWork(
            const GpuDrivenProducerWorkContext& context) = 0;
        virtual GpuDrivenProducerFrameOutput BuildFrameOutput() const = 0;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
