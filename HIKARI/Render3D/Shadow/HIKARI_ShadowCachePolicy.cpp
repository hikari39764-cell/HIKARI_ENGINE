#include "Render3D/Shadow/HIKARI_ShadowCachePolicy.h"

#include <cmath>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"

namespace HIKARI::SHADOW {

    namespace {
        bool AlmostEqualMat4(const MATH::Mat4& lhs, const MATH::Mat4& rhs) {
            constexpr float kEpsilon = 0.0001f;
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    if (std::fabs(lhs.m[col][row] - rhs.m[col][row]) > kEpsilon) {
                        return false;
                    }
                }
            }
            return true;
        }
    }

    void ShadowSourceCacheState::Invalidate() {
        identity_ = nullptr;
        layoutVersion_ = 0;
        sourceVersion_ = 0;
        dirtyBaseVersion_ = 0;
        instanceCount_ = 0;
        valid_ = false;
    }

    bool ShadowSourceCacheState::Matches(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) const {

        return
            valid_ &&
            source != nullptr &&
            identity_ == source &&
            layoutVersion_ == source->layoutVersion &&
            sourceVersion_ == source->sourceVersion &&
            dirtyBaseVersion_ == source->dirtyBaseSourceVersion &&
            instanceCount_ == source->sourceInstanceCount;
    }

    void ShadowSourceCacheState::Capture(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) {

        identity_ = &source;
        layoutVersion_ = source.layoutVersion;
        sourceVersion_ = source.sourceVersion;
        dirtyBaseVersion_ = source.dirtyBaseSourceVersion;
        instanceCount_ = source.sourceInstanceCount;
        valid_ = true;
    }

    void ShadowCachePolicy::BeginFrame() {
        hitThisFrame_ = false;
    }

    void ShadowCachePolicy::Invalidate() {
        valid_ = false;
        hitThisFrame_ = false;
        finalMatchesStaticCache_ = false;
        missReasonFlags_ = ShadowCacheMissReasonInvalid;
    }

    bool ShadowCachePolicy::EvaluateReuse(const ShadowCacheReuseInput& input) {
        uint32_t reason = ShadowCacheMissReasonNone;
        if (!input.hasStaticWork) {
            reason |= ShadowCacheMissReasonNoStaticWork;
        }
        if (!valid_) {
            reason |= ShadowCacheMissReasonInvalid;
        }
        if (!input.resourcesReady) {
            reason |= ShadowCacheMissReasonResource;
        }
        if (!input.stateReusable) {
            reason |= ShadowCacheMissReasonState;
        }
        if (key_.resolution != input.key.resolution) {
            reason |= ShadowCacheMissReasonResolution;
        }
        if (key_.layoutVersion != input.key.layoutVersion) {
            reason |= ShadowCacheMissReasonLayout;
        }
        if (key_.sourceVersion != input.key.sourceVersion) {
            reason |= ShadowCacheMissReasonSource;
        }
        if (key_.sourceInstanceCount != input.key.sourceInstanceCount) {
            reason |= ShadowCacheMissReasonInstanceCount;
        }
        if (!AlmostEqualMat4(key_.lightViewProj, input.key.lightViewProj)) {
            reason |= ShadowCacheMissReasonMatrix;
        }
        if (input.hasStaticDirtyRanges) {
            reason |= ShadowCacheMissReasonStaticDirty;
        }

        missReasonFlags_ = reason;
        return reason == ShadowCacheMissReasonNone;
    }

    void ShadowCachePolicy::MarkHit() {
        hitThisFrame_ = true;
        missReasonFlags_ = ShadowCacheMissReasonNone;
        lastMissReasonFlags_ = ShadowCacheMissReasonNone;
        ++hitCount_;
    }

    void ShadowCachePolicy::MarkMiss() {
        hitThisFrame_ = false;
        valid_ = false;
        finalMatchesStaticCache_ = false;
        if (missReasonFlags_ == ShadowCacheMissReasonNone) {
            missReasonFlags_ = ShadowCacheMissReasonInvalid;
        }
        lastMissReasonFlags_ = missReasonFlags_;
        ++missCount_;
    }

    void ShadowCachePolicy::MarkValid(const ShadowCacheKey& key) {
        valid_ = true;
        hitThisFrame_ = false;
        missReasonFlags_ = ShadowCacheMissReasonNone;
        key_ = key;
        ++updateCount_;
    }

    void ShadowCachePolicy::RecordCopy(bool finalMatchesStaticCache) {
        finalMatchesStaticCache_ = finalMatchesStaticCache;
        ++copyCount_;
    }

    void ShadowCachePolicy::SetFinalMatchesStaticCache(bool matches) {
        finalMatchesStaticCache_ = matches;
    }

    void ShadowCachePolicy::PublishStats(ShadowMapDebugStats& stats) const {
        stats.shadowCacheValid = valid_;
        stats.shadowCacheHit = hitThisFrame_;
        stats.shadowCacheMissReasonFlags = missReasonFlags_;
        stats.shadowCacheLastMissReasonFlags = lastMissReasonFlags_;
        stats.shadowCacheHitCount = hitCount_;
        stats.shadowCacheMissCount = missCount_;
        stats.shadowStaticCacheCopyCount = copyCount_;
        stats.shadowStaticCacheUpdateCount = updateCount_;
    }

} // namespace HIKARI::SHADOW
