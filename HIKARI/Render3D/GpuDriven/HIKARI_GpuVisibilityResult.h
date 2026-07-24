#pragma once

#include <array>
#include <cstddef>

#include <d3d12.h>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenCommandBucket.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenPass.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuVisibilityBucketResult {
        size_t sourceInstanceCount = 0;
        size_t visibleCommandCount = 0;
        size_t visibleCommandOverflowCount = 0;
        size_t commandCapacity = 0;
        UINT64 commandCounterOffset = 0;
        bool gpuCounterBacked = false;
        bool visibleCommandCountKnown = false;

        bool HasGpuCounter() const {
            return gpuCounterBacked && commandCapacity != 0;
        }

        bool HasKnownVisibleCommands() const {
            return visibleCommandCountKnown && visibleCommandCount != 0;
        }
    };

    struct GpuVisibilityPassResult {
        std::array<GpuVisibilityBucketResult, kGpuDrivenCommandBucketCount> buckets{};
        size_t submittedDrawSeedCount = 0;

        size_t GetSourceInstanceCount(GpuDrivenCommandBucket bucket) const {
            return buckets[ToCommandBucketIndex(bucket)].sourceInstanceCount;
        }

        bool HasKnownBucketSourceCounts() const {
            size_t total = 0;
            for (const GpuVisibilityBucketResult& bucket : buckets) {
                total += bucket.sourceInstanceCount;
            }
            return total != 0;
        }

        bool HasSourceForBucket(GpuDrivenCommandBucket bucket) const {
            if (!HasKnownBucketSourceCounts()) {
                return submittedDrawSeedCount != 0;
            }
            return GetSourceInstanceCount(bucket) != 0;
        }

        bool HasGpuCommandCounters() const {
            for (const GpuVisibilityBucketResult& bucket : buckets) {
                if (bucket.HasGpuCounter()) {
                    return true;
                }
            }
            return false;
        }

        bool HasKnownVisibleCommandCounts() const {
            for (const GpuVisibilityBucketResult& bucket : buckets) {
                if (bucket.visibleCommandCountKnown) {
                    return true;
                }
            }
            return false;
        }

        size_t CountKnownVisibleCommands() const {
            size_t count = 0;
            for (const GpuVisibilityBucketResult& bucket : buckets) {
                if (bucket.visibleCommandCountKnown) {
                    count += bucket.visibleCommandCount;
                }
            }
            return count;
        }

        size_t CountKnownVisibleCommandOverflows() const {
            size_t count = 0;
            for (const GpuVisibilityBucketResult& bucket : buckets) {
                if (bucket.visibleCommandCountKnown) {
                    count += bucket.visibleCommandOverflowCount;
                }
            }
            return count;
        }
    };

    struct GpuVisibilityResult {
        ID3D12Resource* visibleInstanceBuffer = nullptr;
        ID3D12Resource* visibleClusterRangeBuffer = nullptr;
        ID3D12Resource* visibleMeshletRangeBuffer = nullptr;
        ID3D12Resource* visibleMeshletClusterListBuffer = nullptr;
        ID3D12Resource* counterBuffer = nullptr;

        std::array<GpuVisibilityPassResult, kGpuDrivenPassCount> passes{};

        const GpuVisibilityPassResult& GetPass(
            GpuDrivenPassKind pass) const {

            return passes[ToPassIndex(pass)];
        }

        GpuVisibilityPassResult& GetPass(
            GpuDrivenPassKind pass) {

            return passes[ToPassIndex(pass)];
        }

        size_t GetSourceInstanceCount(
            GpuDrivenPassKind pass,
            GpuDrivenCommandBucket bucket) const {

            return GetPass(pass).GetSourceInstanceCount(bucket);
        }

        bool HasSourceForBucket(
            GpuDrivenPassKind pass,
            GpuDrivenCommandBucket bucket) const {

            return GetPass(pass).HasSourceForBucket(bucket);
        }
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
