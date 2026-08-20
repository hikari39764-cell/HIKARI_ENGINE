#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Vfx/HIKARI_ActionPostReactionFrameSource.h"
#include "Vfx/Post/HIKARI_PostCommon.h"

namespace HIKARI::POST {

    // Frame-local copy of the perfect-guard reaction.  The provider is fed
    // before HDR resolve and never stores a pointer into a scene service.
    // The projected center is deliberately captured with the render camera
    // used for the same frame so temporal/post processing cannot observe a
    // later camera pose.
    class ActionScreenImpulseProvider final {
    public:
        static constexpr uint32_t kMaximumWaveCount =
            RENDER3D::COMBAT_VFX::kMaximumPerfectGuardShockwaves;

        struct Wave {
            MATH::Vec2 centerUv{};
            float ageNormalized = 0.0f;
            float active = 0.0f;
            float maximumScreenRadius = 0.46f;
            float ringThickness = 0.055f;
            float distortionStrength = 0.020f;
            float intensity = 1.0f;
            MATH::Vec3 color{ 0.32f, 0.78f, 1.0f };
            float chromaticShift = 0.0035f;
            float flashIntensity = 0.42f;
        };

        // A frame-local mask supplied by the combat trail/impact renderer.  The
        // producer owns the resource and its state transition; the post pass
        // copies this CPU SRV into its own two-slot heap and never retains the
        // view beyond the current resolve.
        struct Mask {
            RENDER3D::RenderTextureView texture{};
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
            uint64_t frameIndex = 0u;
            float strength = 0.85f;
            float blur = 1.0f;

            bool IsValid(uint64_t currentFrame) const noexcept {
                return frameIndex == currentFrame &&
                    texture.resource != nullptr &&
                    texture.IsSampleable() &&
                    srvCpu.ptr != 0u;
            }
        };

        struct DebugState {
            bool requested = false;
            bool providerReady = false;
            bool pipelineReady = false;
            bool executed = false;
            bool composited = false;
            bool failed = false;
            uint64_t frameIndex = 0u;
            uint32_t gfxFrameSlot = 0u;
            uint32_t activeWaveCount = 0u;
            uint64_t sourceFrameIndex = 0u;
            uint64_t submitFrameIndex = 0u;
            uint32_t sourceWaveCount = 0u;
            uint32_t projectedWaveCount = 0u;
            uint32_t droppedWaveCount = 0u;
            uint32_t projectionRejectCount = 0u;
            uint32_t lastRejectReason = 0u;
            bool bypassed = false;
            bool maskProvided = false;
            bool maskCompatible = false;
            bool maskApplied = false;
        };

        void BeginFrame(uint64_t frameIndex) noexcept {
            // Multiple presentation owners can touch the global post stack in
            // one host frame.  Beginning an already-open frame must therefore
            // be idempotent; clearing here used to silently erase a guard wave
            // or trail mask submitted by an earlier render view.
            if (frameOpen_ && frameIndex_ == frameIndex) {
                return;
            }
            frameIndex_ = frameIndex;
            frameOpen_ = true;
            sourceFrameIndex_ = 0u;
            sourceSubmitted_ = false;
            count_ = 0u;
            waves_ = {};
            mask_ = {};
            debugState_ = {};
            debugState_.frameIndex = frameIndex;
            debugState_.submitFrameIndex = frameIndex;
            debugState_.maskProvided = mask_.texture.resource != nullptr;
            debugState_.maskCompatible = mask_.IsValid(frameIndex);
            debugState_.requested = mask_.IsValid(frameIndex);
        }

        void Submit(
            uint64_t frameIndex,
            const MATH::Mat4& viewProj,
            const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource& source)
            noexcept {
            BeginFrame(frameIndex);
            const uint32_t sourceCount = std::min<uint32_t>(
                source.count,
                kMaximumWaveCount);
            debugState_.sourceWaveCount = std::min<uint32_t>(
                kMaximumWaveCount,
                debugState_.sourceWaveCount + sourceCount);
            debugState_.submitFrameIndex = frameIndex;
            debugState_.requested = count_ != 0u || sourceCount != 0u ||
                mask_.IsValid(frameIndex);
            if (sourceCount == 0u) {
                return;
            }
            if (source.frameIndex != 0u &&
                source.frameIndex != frameIndex) {
                // Scene reactions carry the monotonic TIME frame token.  Do
                // not let a stale reaction survive a camera/post frame;
                // preserve any valid mask submitted for this frame.
                RegisterProjectionReject(5u);
                debugState_.droppedWaveCount += sourceCount;
                return;
            }
            sourceFrameIndex_ = source.frameIndex;
            sourceSubmitted_ = true;
            debugState_.sourceFrameIndex = sourceFrameIndex_;
            for (uint32_t index = 0u;
                index < sourceCount && count_ < kMaximumWaveCount;
                ++index) {
                const auto& sourceWave = source.waves[index];
                if (!std::isfinite(sourceWave.worldPosition.x) ||
                    !std::isfinite(sourceWave.worldPosition.y) ||
                    !std::isfinite(sourceWave.worldPosition.z)) {
                    RegisterProjectionReject(1u);
                    continue;
                }
                const MATH::Vec4 clip = viewProj.TransformPoint({
                    sourceWave.worldPosition.x,
                    sourceWave.worldPosition.y,
                    sourceWave.worldPosition.z,
                    1.0f });
                if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
                    !std::isfinite(clip.z) || !std::isfinite(clip.w) ||
                    clip.w <= 1.0e-5f) {
                    RegisterProjectionReject(2u);
                    continue;
                }
                // HIKARI uses the D3D zero-to-one depth convention.  A
                // point behind or outside the depth range cannot contribute
                // to the screen wave and should be visible in diagnostics.
                if (clip.z < 0.0f || clip.z > clip.w) {
                    RegisterProjectionReject(3u);
                    continue;
                }
                const float inverseW = 1.0f / clip.w;
                const float ndcX = clip.x * inverseW;
                const float ndcY = clip.y * inverseW;
                if (!std::isfinite(ndcX) || !std::isfinite(ndcY) ||
                    ndcX < -8.0f || ndcX > 8.0f ||
                    ndcY < -8.0f || ndcY > 8.0f) {
                    RegisterProjectionReject(4u);
                    continue;
                }

                Wave candidate{};
                candidate.centerUv = {
                    ndcX * 0.5f + 0.5f,
                    -ndcY * 0.5f + 0.5f };
                const float duration = std::clamp(
                    std::isfinite(sourceWave.durationSeconds)
                        ? sourceWave.durationSeconds
                        : 0.52f,
                    0.03f,
                    2.0f);
                candidate.ageNormalized = std::clamp(
                    (std::isfinite(sourceWave.ageSeconds)
                        ? sourceWave.ageSeconds
                        : 0.0f) / duration,
                    0.0f,
                    1.0f);
                candidate.active = 1.0f;
                candidate.maximumScreenRadius = std::clamp(
                    std::isfinite(sourceWave.maximumScreenRadius)
                        ? sourceWave.maximumScreenRadius
                        : 0.46f,
                    0.02f,
                    1.5f);
                candidate.ringThickness = std::clamp(
                    std::isfinite(sourceWave.ringThickness)
                        ? sourceWave.ringThickness
                        : 0.055f,
                    0.001f,
                    0.35f);
                candidate.distortionStrength = std::clamp(
                    std::isfinite(sourceWave.distortionStrength)
                        ? sourceWave.distortionStrength
                        : 0.020f,
                    0.0f,
                    0.25f);
                candidate.intensity = std::clamp(
                    std::isfinite(sourceWave.intensity)
                        ? sourceWave.intensity
                        : 1.0f,
                    0.0f,
                    8.0f);
                candidate.color = {
                    std::clamp(
                        std::isfinite(sourceWave.color.x)
                            ? sourceWave.color.x : 0.32f,
                        0.0f,
                        8.0f),
                    std::clamp(
                        std::isfinite(sourceWave.color.y)
                            ? sourceWave.color.y : 0.78f,
                        0.0f,
                        8.0f),
                    std::clamp(
                        std::isfinite(sourceWave.color.z)
                            ? sourceWave.color.z : 1.0f,
                        0.0f,
                        8.0f) };
                candidate.chromaticShift = std::clamp(
                    std::isfinite(sourceWave.chromaticShift)
                        ? sourceWave.chromaticShift
                        : 0.0035f,
                    0.0f,
                    0.05f);
                candidate.flashIntensity = std::clamp(
                    std::isfinite(sourceWave.flashIntensity)
                        ? sourceWave.flashIntensity
                        : 0.42f,
                    0.0f,
                    4.0f);

                // A scene can be submitted by more than one compatible render
                // view.  Merge distinct reactions, but do not amplify the same
                // wave merely because the primary view was traversed twice.
                bool duplicate = false;
                for (uint32_t existingIndex = 0u;
                    existingIndex < count_;
                    ++existingIndex) {
                    const Wave& existing = waves_[existingIndex];
                    const float deltaX = existing.centerUv.x - candidate.centerUv.x;
                    const float deltaY = existing.centerUv.y - candidate.centerUv.y;
                    if (deltaX * deltaX + deltaY * deltaY <= 1.0e-8f &&
                        std::abs(existing.ageNormalized -
                            candidate.ageNormalized) <= 1.0e-4f &&
                        std::abs(existing.maximumScreenRadius -
                            candidate.maximumScreenRadius) <= 1.0e-4f) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate && count_ < kMaximumWaveCount) {
                    waves_[count_++] = candidate;
                }
            }
            debugState_.projectedWaveCount = count_;
            debugState_.activeWaveCount = count_;
            debugState_.droppedWaveCount = std::max<uint32_t>(
                debugState_.droppedWaveCount,
                sourceCount > count_ ? sourceCount - count_ : 0u);
        }

        void SubmitMask(const Mask& mask) noexcept {
            if (mask.frameIndex == 0u) {
                debugState_.maskProvided =
                    debugState_.maskProvided ||
                    mask.texture.resource != nullptr;
                debugState_.maskCompatible = false;
                return;
            }
            if (!frameOpen_ || frameIndex_ != mask.frameIndex) {
                BeginFrame(mask.frameIndex);
            }
            Mask sanitized = mask;
            sanitized.strength = std::clamp(
                std::isfinite(sanitized.strength)
                    ? sanitized.strength
                    : 0.85f,
                0.0f,
                4.0f);
            sanitized.blur = std::clamp(
                std::isfinite(sanitized.blur) ? sanitized.blur : 1.0f,
                0.05f,
                8.0f);
            debugState_.maskProvided = debugState_.maskProvided ||
                sanitized.texture.resource != nullptr;
            if (!sanitized.IsValid(frameIndex_)) {
                // A secondary view must not erase a valid primary-view mask
                // with an empty or stale handoff in the same host frame.
                debugState_.maskCompatible = mask_.IsValid(frameIndex_);
                return;
            }
            if (mask_.IsValid(frameIndex_) &&
                mask_.texture.resource == sanitized.texture.resource) {
                mask_.strength = (std::max)(
                    mask_.strength,
                    sanitized.strength);
                mask_.blur = (std::max)(mask_.blur, sanitized.blur);
            }
            else {
                mask_ = sanitized;
            }
            debugState_.maskCompatible = true;
            debugState_.requested = true;
        }

        void SubmitMask(
            const RENDER3D::RenderTextureView& texture,
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpu,
            uint64_t frameIndex,
            float strength = 0.85f,
            float blur = 1.0f) noexcept {
            Mask mask{};
            mask.texture = texture;
            mask.srvCpu = srvCpu;
            mask.frameIndex = frameIndex;
            mask.strength = strength;
            mask.blur = blur;
            SubmitMask(mask);
        }

        bool IsCurrent(uint64_t frameIndex) const noexcept {
            return frameIndex_ == frameIndex &&
                ((sourceSubmitted_ && count_ != 0u) ||
                    mask_.IsValid(frameIndex));
        }

        uint64_t GetFrameIndex() const noexcept { return frameIndex_; }
        uint64_t GetSourceFrameIndex() const noexcept {
            return sourceFrameIndex_;
        }
        uint32_t GetCount() const noexcept { return count_; }
        bool HasSource() const noexcept {
            return sourceSubmitted_ && count_ != 0u;
        }
        const Mask& GetMask() const noexcept { return mask_; }
        const Wave& GetWave(uint32_t index) const noexcept {
            return waves_[(std::min)(index, kMaximumWaveCount - 1u)];
        }

        void ApplyParams(CommonParams& params) const noexcept {
            params.combo = static_cast<float>(count_);
            for (uint32_t index = 0u; index < kMaximumWaveCount; ++index) {
                const Wave& wave = waves_[index];
                const uint32_t base = index * 4u;
                params.user[base + 0u] = {
                    wave.centerUv.x,
                    wave.centerUv.y,
                    wave.ageNormalized,
                    wave.active };
                params.user[base + 1u] = {
                    wave.maximumScreenRadius,
                    wave.ringThickness,
                    wave.distortionStrength,
                    wave.intensity };
                params.user[base + 2u] = {
                    wave.color.x,
                    wave.color.y,
                    wave.color.z,
                    wave.chromaticShift };
                params.user[base + 3u] = {
                    wave.flashIntensity,
                    0.0f,
                    0.0f,
                    0.0f };
            }
            // The fourth wave's unused payload lanes carry the optional mask
            // controls.  Wave 3 only consumes .x (flashIntensity), so the
            // existing parameter ABI remains intact for the guard-only path.
            params.user[15].y = mask_.IsValid(frameIndex_)
                ? 1.0f : 0.0f;
            params.user[15].z = mask_.IsValid(frameIndex_)
                ? mask_.strength : 0.0f;
            params.user[15].w = mask_.IsValid(frameIndex_)
                ? mask_.blur : 0.0f;
        }

        DebugState& MutableDebugState() noexcept { return debugState_; }
        const DebugState& GetDebugState() const noexcept {
            return debugState_;
        }

    private:
        void RegisterProjectionReject(uint32_t reason) noexcept {
            ++debugState_.projectionRejectCount;
            debugState_.lastRejectReason = reason;
        }

        uint64_t frameIndex_ = 0u;
        bool frameOpen_ = false;
        uint64_t sourceFrameIndex_ = 0u;
        bool sourceSubmitted_ = false;
        uint32_t count_ = 0u;
        std::array<Wave, kMaximumWaveCount> waves_{};
        Mask mask_{};
        DebugState debugState_{};
    };

} // namespace HIKARI::POST
