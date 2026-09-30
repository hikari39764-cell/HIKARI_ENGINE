#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Shadow/HIKARI_ShadowCacheTypes.h"
#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct GpuDrivenSceneSource;
}

namespace HIKARI::SHADOW {

struct ShadowMapDebugStats;

class ShadowSourceCacheState final {
public:
    void Invalidate();
    bool Matches(const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) const;
    bool CanPatch(const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) const;
    void Capture(const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source);

private:
    const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* identity_ = nullptr;
    uint64_t layoutVersion_ = 0;
    uint64_t sourceVersion_ = 0;
    size_t instanceCount_ = 0;
    bool valid_ = false;
};

struct ShadowCacheKey {
    uint64_t layoutVersion = 0;
    uint64_t sourceVersion = 0;
    size_t sourceInstanceCount = 0;
    uint32_t resolution = 0;
    std::array<MATH::Mat4, kMaxDirectionalShadowCascades> lightViewProj{};
    uint32_t cascadeCount = 1u;
};

struct ShadowCacheReuseInput {
    ShadowCacheKey key{};
    bool hasStaticWork = false;
    bool resourcesReady = false;
    bool stateReusable = false;
    bool hasStaticDirtyRanges = false;
};

class ShadowCachePolicy final {
public:
    void BeginFrame();
    void Invalidate();
    bool EvaluateReuse(const ShadowCacheReuseInput& input);
    void MarkHit();
    void MarkMiss(uint64_t logicalFrameIndex);
    void MarkValid(const ShadowCacheKey& key);
    void RecordCopy(bool finalMatchesStaticCache);
    void SetFinalMatchesStaticCache(bool matches);

    bool IsValid() const { return valid_; }
    bool WasHitThisFrame() const { return hitThisFrame_; }
    bool FinalMatchesStaticCache() const { return finalMatchesStaticCache_; }

    void PublishStats(ShadowMapDebugStats& stats) const;

private:
    bool valid_ = false;
    bool hitThisFrame_ = false;
    bool finalMatchesStaticCache_ = false;
    uint32_t missReasonFlags_ = ShadowCacheMissReasonNone;
    uint32_t lastMissReasonFlags_ = ShadowCacheMissReasonNone;
    uint64_t lastMissFrameIndex_ = 0u;
    ShadowCacheKey key_{};
    size_t hitCount_ = 0;
    size_t missCount_ = 0;
    size_t copyCount_ = 0;
    size_t updateCount_ = 0;
};

} // namespace HIKARI::SHADOW
