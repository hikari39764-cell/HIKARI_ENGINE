#pragma once

#include <cstddef>
#include <cstdint>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    enum class GpuDrivenPassKind : uint32_t {
        ForwardOpaque = 0,
        DepthAware = 1,
        Transparent = 2,
        Shadow = 3,
        GeometryAux = 4,
        ReflectionCapture = 5,
        DepthPrepass = 6,
        Debug = 7,

        ForwardDepthAware = DepthAware,
        ForwardTransparent = Transparent,
        Count = 8,
    };

    constexpr size_t kGpuDrivenPassCount =
        static_cast<size_t>(GpuDrivenPassKind::Count);

    constexpr uint32_t MakeGpuDrivenPassMask(GpuDrivenPassKind passKind) {
        return 1u << static_cast<uint32_t>(passKind);
    }

    size_t ToPassIndex(GpuDrivenPassKind passKind);

} // namespace HIKARI::RENDER3D::GPUDRIVEN
