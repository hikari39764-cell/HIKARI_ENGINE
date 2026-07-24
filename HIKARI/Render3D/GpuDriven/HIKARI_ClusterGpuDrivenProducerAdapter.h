#pragma once

#include "Render3D/GpuDriven/HIKARI_GpuDrivenProducer.h"

namespace HIKARI::RENDER3D::CLUSTER {
    class ClusterGpuCullingPass;
    struct ClusterGpuCullingPassStats;
}

namespace HIKARI::RENDER3D::GPUDRIVEN {

    class ClusterGpuDrivenProducerAdapter final : public IGpuDrivenProducer {
    public:
        void Attach(CLUSTER::ClusterGpuCullingPass* cullingPass);
        bool IsAttached() const;

        GpuDrivenProducerKind GetProducerKind() const override;
        void BeginFrame(bool collectCounterReadback) override;
        GpuDrivenProducerWorkResult DispatchWork(
            const GpuDrivenProducerWorkContext& context) override;
        GpuDrivenProducerFrameOutput BuildFrameOutput() const override;

        const CLUSTER::ClusterGpuCullingPassStats* GetClusterStats() const;

    private:
        CLUSTER::ClusterGpuCullingPass* cullingPass_ = nullptr;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
