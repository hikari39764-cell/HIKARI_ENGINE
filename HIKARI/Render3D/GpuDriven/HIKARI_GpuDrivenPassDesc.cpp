#include "Render3D/GpuDriven/HIKARI_GpuDrivenPassDesc.h"

#include <array>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        constexpr GpuDrivenPassDesc kFallbackDesc{
            GpuDrivenPassKind::Debug,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
        };

        constexpr std::array<GpuDrivenPassDesc, kGpuDrivenPassCount> kPassDescs{ {
            { GpuDrivenPassKind::ForwardOpaque, true, false, true, true, false, true, true, false },
            { GpuDrivenPassKind::DepthAware, true, false, true, true, false, true, true, false },
            { GpuDrivenPassKind::Transparent, true, false, true, true, true, true, true, true },
            { GpuDrivenPassKind::Shadow, false, true, true, true, false, true, true, false },
            { GpuDrivenPassKind::GeometryAux, true, false, true, true, false, false, true, false },
            { GpuDrivenPassKind::ReflectionCapture, true, false, true, true, false, false, false, false },
            { GpuDrivenPassKind::DepthPrepass, true, false, true, true, false, false, false, false },
            { GpuDrivenPassKind::Debug, false, false, false, false, false, false, false, false },
        } };
    }

    const GpuDrivenPassDesc& GetGpuDrivenPassDesc(GpuDrivenPassKind pass) {
        const size_t index = ToPassIndex(pass);
        if (index >= kPassDescs.size()) {
            return kFallbackDesc;
        }
        return kPassDescs[index];
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
