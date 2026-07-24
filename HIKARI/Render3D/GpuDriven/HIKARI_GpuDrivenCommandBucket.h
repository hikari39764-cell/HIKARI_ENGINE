#pragma once

#include <cstddef>
#include <cstdint>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    enum class GpuDrivenCommandBucket : uint32_t {
        BackFaceCulled = 0,
        DoubleSided = 1,
        Count = 2,
    };

    constexpr size_t kGpuDrivenCommandBucketCount =
        static_cast<size_t>(GpuDrivenCommandBucket::Count);

    constexpr size_t ToCommandBucketIndex(GpuDrivenCommandBucket bucket) {
        const size_t index = static_cast<size_t>(bucket);
        return index < kGpuDrivenCommandBucketCount ? index : 0u;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
