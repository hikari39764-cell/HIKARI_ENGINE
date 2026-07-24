#pragma once

#include <array>
#include <cstddef>

#include <d3d12.h>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenCommandBucket.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenPass.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenCommandBucketLayout {
        UINT64 meshDispatchArgumentOffset = 0;
        UINT64 counterOffset = 0;
    };

    struct GpuDrivenCommandPassLayout {
        size_t commandBucketCapacity = 0;
        std::array<GpuDrivenCommandBucketLayout, kGpuDrivenCommandBucketCount> buckets{};

        const GpuDrivenCommandBucketLayout& GetBucket(
            GpuDrivenCommandBucket bucket) const {

            return buckets[ToCommandBucketIndex(bucket)];
        }

        GpuDrivenCommandBucketLayout& GetBucket(
            GpuDrivenCommandBucket bucket) {

            return buckets[ToCommandBucketIndex(bucket)];
        }
    };

    struct GpuDrivenCommandLayout {
        std::array<GpuDrivenCommandPassLayout, kGpuDrivenPassCount> passes{};

        const GpuDrivenCommandPassLayout& GetPass(
            GpuDrivenPassKind pass) const {

            return passes[ToPassIndex(pass)];
        }

        GpuDrivenCommandPassLayout& GetPass(
            GpuDrivenPassKind pass) {

            return passes[ToPassIndex(pass)];
        }
    };

    struct GpuCommandBuildResult {
        ID3D12Resource* surfaceDrawIndexedArgs = nullptr;
        ID3D12Resource* surfaceSkinnedDrawIndexedArgs = nullptr;
        ID3D12Resource* surfaceDrawIndexedCounter = nullptr;
        ID3D12Resource* meshDispatchArgs = nullptr;

        ID3D12CommandSignature* surfaceDrawIndexedSignature = nullptr;
        ID3D12CommandSignature* surfaceSkinnedDrawIndexedSignature = nullptr;
        ID3D12CommandSignature* meshDispatchSignature = nullptr;

        GpuDrivenCommandLayout layout{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
