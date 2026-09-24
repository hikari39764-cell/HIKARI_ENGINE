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

// Frame-local copy of combat screen reactions. The provider is fed
// before HDR resolve and never stores a pointer into a scene service.
// The projected center is deliberately captured with the render camera
// used for the same frame so temporal/post processing cannot observe a
// later camera pose.
class ActionScreenImpulseProvider final {
public:
    static constexpr uint32_t kMaximumWaveCount = RENDER3D::COMBAT_VFX::kMaximumPerfectGuardShockwaves;

    struct Wave {
        MATH::Vec2 centerUv{};
        float ageNormalized = 0.0f;
        float active = 0.0f;
        float maximumScreenRadius = 0.46f;
        float ringThickness = 0.055f;
        float distortionStrength = 0.020f;
        float intensity = 1.0f;
        MATH::Vec3 color{0.32f, 0.78f, 1.0f};
        float chromaticShift = 0.0035f;
        float flashIntensity = 0.42f;
        float waveFrequency = 1.0f;
        float waveAmplitude = 1.0f;
        float waveDamping = 1.0f;
        uint32_t waveCount = 1u;
        bool converging = false;
    };

    struct CinematicGrade {
        float weight = 0.0f;
        float monochromeStrength = 0.0f;
        float chromaticSeparationPixels = 0.0f;
        float contrastStrength = 0.0f;
        float boostWeight = 0.0f;
        float boostEdgeIntensity = 0.0f;
        float boostEdgeWidth = 0.20f;
        float boostChromaticSeparationPixels = 0.0f;

        bool IsActive() const noexcept { return weight > 0.0f || (boostWeight > 0.0f && boostEdgeIntensity > 0.0f); }
    };

    // A frame-local texture supplied by the combat trail/impact renderer.
    // The producer owns the resource and its state transition; the post
    // pass copies this CPU SRV into its own frame-local descriptor heap and
    // never retains the view beyond the current resolve. The same value
    // contract is used for the distortion mask and the HDR accent.
    struct Mask {
        RENDER3D::RenderTextureView texture{};
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
        uint64_t frameIndex = 0u;
        float strength = 0.85f;
        float blur = 1.0f;

        bool IsValid(uint64_t currentFrame) const noexcept {
            return frameIndex == currentFrame && texture.resource != nullptr && texture.IsSampleable() &&
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
        bool accentProvided = false;
        bool accentCompatible = false;
        bool accentApplied = false;
        bool cinematicGradeProvided = false;
        bool cinematicGradeApplied = false;
        float cinematicGradeWeight = 0.0f;
        bool boostGradeProvided = false;
        bool boostGradeApplied = false;
        float boostGradeWeight = 0.0f;
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
        accent_ = {};
        cinematicGrade_ = {};
        debugState_ = {};
        debugState_.frameIndex = frameIndex;
        debugState_.submitFrameIndex = frameIndex;
        debugState_.maskProvided = mask_.texture.resource != nullptr;
        debugState_.maskCompatible = mask_.IsValid(frameIndex);
        debugState_.accentProvided = accent_.texture.resource != nullptr;
        debugState_.accentCompatible = accent_.IsValid(frameIndex);
        debugState_.requested = mask_.IsValid(frameIndex) || accent_.IsValid(frameIndex);
    }

    void Submit(uint64_t frameIndex, const MATH::Mat4& viewProj,
                const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource& source) noexcept {
        const uint32_t sourceCount = std::min<uint32_t>(source.count, kMaximumWaveCount);
        const bool sourceHasCinematicGrade = source.HasActiveCinematicGrade();
        const bool sourceHasBoostGrade = source.HasActiveBoostGrade();
        if (!frameOpen_) {
            BeginFrame(frameIndex);
        } else if (frameIndex_ != frameIndex) {
            // BeginFrame is the canonical monotonic frame owner. A stale
            // or out-of-order secondary view must never rewind the
            // provider and erase work already accepted for this frame.
            debugState_.submitFrameIndex = frameIndex;
            debugState_.sourceWaveCount =
                std::min<uint32_t>(kMaximumWaveCount, debugState_.sourceWaveCount + sourceCount);
            debugState_.droppedWaveCount += sourceCount;
            RegisterProjectionReject(5u);
            return;
        }
        debugState_.sourceWaveCount = std::min<uint32_t>(kMaximumWaveCount, debugState_.sourceWaveCount + sourceCount);
        debugState_.submitFrameIndex = frameIndex;
        debugState_.requested = count_ != 0u || sourceCount != 0u || sourceHasCinematicGrade || sourceHasBoostGrade ||
                                mask_.IsValid(frameIndex) || accent_.IsValid(frameIndex);
        debugState_.cinematicGradeProvided = debugState_.cinematicGradeProvided || sourceHasCinematicGrade;
        debugState_.boostGradeProvided = debugState_.boostGradeProvided || sourceHasBoostGrade;
        if (sourceCount == 0u && !sourceHasCinematicGrade && !sourceHasBoostGrade) {
            return;
        }
        if (source.frameIndex != 0u && source.frameIndex != frameIndex) {
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
        if (sourceHasCinematicGrade) {
            CinematicGrade candidate{};
            candidate.weight = std::clamp(
                std::isfinite(source.cinematicGrade.weight) ? source.cinematicGrade.weight : 0.0f, 0.0f, 1.0f);
            candidate.monochromeStrength = std::clamp(std::isfinite(source.cinematicGrade.monochromeStrength)
                                                          ? source.cinematicGrade.monochromeStrength
                                                          : 0.0f,
                                                      0.0f, 1.0f);
            candidate.chromaticSeparationPixels =
                std::clamp(std::isfinite(source.cinematicGrade.chromaticSeparationPixels)
                               ? source.cinematicGrade.chromaticSeparationPixels
                               : 0.0f,
                           0.0f, 24.0f);
            candidate.contrastStrength = std::clamp(
                std::isfinite(source.cinematicGrade.contrastStrength) ? source.cinematicGrade.contrastStrength : 0.0f,
                0.0f, 2.0f);
            if (candidate.weight > cinematicGrade_.weight) {
                cinematicGrade_ = candidate;
            }
            debugState_.cinematicGradeWeight = cinematicGrade_.weight;
        }
        if (sourceHasBoostGrade) {
            const float candidateWeight =
                std::clamp(std::isfinite(source.boostGrade.weight) ? source.boostGrade.weight : 0.0f, 0.0f, 1.0f);
            if (candidateWeight > cinematicGrade_.boostWeight) {
                cinematicGrade_.boostWeight = candidateWeight;
                cinematicGrade_.boostEdgeIntensity =
                    std::clamp(std::isfinite(source.boostGrade.edgeIntensity) ? source.boostGrade.edgeIntensity : 0.0f,
                               0.0f, 4.0f);
                cinematicGrade_.boostEdgeWidth = std::clamp(
                    std::isfinite(source.boostGrade.edgeWidth) ? source.boostGrade.edgeWidth : 0.20f, 0.01f, 0.80f);
                cinematicGrade_.boostChromaticSeparationPixels =
                    std::clamp(std::isfinite(source.boostGrade.chromaticSeparationPixels)
                                   ? source.boostGrade.chromaticSeparationPixels
                                   : 0.0f,
                               0.0f, 16.0f);
            }
            debugState_.boostGradeWeight = cinematicGrade_.boostWeight;
        }
        for (uint32_t index = 0u; index < sourceCount && count_ < kMaximumWaveCount; ++index) {
            const auto& sourceWave = source.waves[index];
            if (!std::isfinite(sourceWave.worldPosition.x) || !std::isfinite(sourceWave.worldPosition.y) ||
                !std::isfinite(sourceWave.worldPosition.z)) {
                RegisterProjectionReject(1u);
                continue;
            }
            const MATH::Vec4 clip = viewProj.TransformPoint(
                {sourceWave.worldPosition.x, sourceWave.worldPosition.y, sourceWave.worldPosition.z, 1.0f});
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w) ||
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
            if (!std::isfinite(ndcX) || !std::isfinite(ndcY) || ndcX < -8.0f || ndcX > 8.0f || ndcY < -8.0f ||
                ndcY > 8.0f) {
                RegisterProjectionReject(4u);
                continue;
            }

            Wave candidate{};
            candidate.centerUv = {ndcX * 0.5f + 0.5f, -ndcY * 0.5f + 0.5f};
            const float duration =
                std::clamp(std::isfinite(sourceWave.durationSeconds) ? sourceWave.durationSeconds : 0.52f, 0.03f, 2.0f);
            candidate.ageNormalized = std::clamp(
                (std::isfinite(sourceWave.ageSeconds) ? sourceWave.ageSeconds : 0.0f) / duration, 0.0f, 1.0f);
            candidate.active = 1.0f;
            candidate.maximumScreenRadius = std::clamp(
                std::isfinite(sourceWave.maximumScreenRadius) ? sourceWave.maximumScreenRadius : 0.46f, 0.02f, 1.5f);
            candidate.ringThickness =
                std::clamp(std::isfinite(sourceWave.ringThickness) ? sourceWave.ringThickness : 0.055f, 0.001f, 0.35f);
            candidate.distortionStrength = std::clamp(
                std::isfinite(sourceWave.distortionStrength) ? sourceWave.distortionStrength : 0.020f, 0.0f, 0.25f);
            candidate.intensity =
                std::clamp(std::isfinite(sourceWave.intensity) ? sourceWave.intensity : 1.0f, 0.0f, 8.0f);
            candidate.color = {std::clamp(std::isfinite(sourceWave.color.x) ? sourceWave.color.x : 0.32f, 0.0f, 8.0f),
                               std::clamp(std::isfinite(sourceWave.color.y) ? sourceWave.color.y : 0.78f, 0.0f, 8.0f),
                               std::clamp(std::isfinite(sourceWave.color.z) ? sourceWave.color.z : 1.0f, 0.0f, 8.0f)};
            candidate.chromaticShift =
                std::clamp(std::isfinite(sourceWave.chromaticShift) ? sourceWave.chromaticShift : 0.0035f, 0.0f, 0.05f);
            candidate.flashIntensity =
                std::clamp(std::isfinite(sourceWave.flashIntensity) ? sourceWave.flashIntensity : 0.42f, 0.0f, 4.0f);
            candidate.waveFrequency =
                std::clamp(std::isfinite(sourceWave.waveFrequency) ? sourceWave.waveFrequency : 1.0f, 0.05f, 8.0f);
            candidate.waveAmplitude =
                std::clamp(std::isfinite(sourceWave.waveAmplitude) ? sourceWave.waveAmplitude : 1.0f, 0.0f, 8.0f);
            candidate.waveDamping =
                std::clamp(std::isfinite(sourceWave.waveDamping) ? sourceWave.waveDamping : 1.0f, 0.05f, 8.0f);
            candidate.waveCount = std::clamp(sourceWave.waveCount, 1u, 4u);
            candidate.converging =
                (sourceWave.flags & RENDER3D::COMBAT_VFX::kPerfectGuardShockwaveConvergingFlag) != 0u;

            // A scene can be submitted by more than one compatible render
            // view.  Merge distinct reactions, but do not amplify the same
            // wave merely because the primary view was traversed twice.
            bool duplicate = false;
            for (uint32_t existingIndex = 0u; existingIndex < count_; ++existingIndex) {
                const Wave& existing = waves_[existingIndex];
                const float deltaX = existing.centerUv.x - candidate.centerUv.x;
                const float deltaY = existing.centerUv.y - candidate.centerUv.y;
                if (deltaX * deltaX + deltaY * deltaY <= 1.0e-8f &&
                    std::abs(existing.ageNormalized - candidate.ageNormalized) <= 1.0e-4f &&
                    std::abs(existing.maximumScreenRadius - candidate.maximumScreenRadius) <= 1.0e-4f &&
                    existing.converging == candidate.converging) {
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
        debugState_.droppedWaveCount =
            std::max<uint32_t>(debugState_.droppedWaveCount, sourceCount > count_ ? sourceCount - count_ : 0u);
    }

    void SubmitMask(const Mask& mask) noexcept {
        if (mask.frameIndex == 0u) {
            debugState_.maskProvided = debugState_.maskProvided || mask.texture.resource != nullptr;
            debugState_.maskCompatible = false;
            return;
        }
        if (!frameOpen_) {
            BeginFrame(mask.frameIndex);
        } else if (frameIndex_ != mask.frameIndex) {
            debugState_.maskProvided = debugState_.maskProvided || mask.texture.resource != nullptr;
            debugState_.maskCompatible = mask_.IsValid(frameIndex_);
            return;
        }
        Mask sanitized = mask;
        sanitized.strength = std::clamp(std::isfinite(sanitized.strength) ? sanitized.strength : 0.85f, 0.0f, 4.0f);
        sanitized.blur = std::clamp(std::isfinite(sanitized.blur) ? sanitized.blur : 1.0f, 0.0f, 8.0f);
        debugState_.maskProvided = debugState_.maskProvided || sanitized.texture.resource != nullptr;
        if (!sanitized.IsValid(frameIndex_)) {
            // A secondary view must not erase a valid primary-view mask
            // with an empty or stale handoff in the same host frame.
            debugState_.maskCompatible = mask_.IsValid(frameIndex_);
            return;
        }
        if (mask_.IsValid(frameIndex_) && mask_.texture.resource == sanitized.texture.resource) {
            mask_.strength = (std::max)(mask_.strength, sanitized.strength);
            mask_.blur = (std::max)(mask_.blur, sanitized.blur);
        } else {
            mask_ = sanitized;
        }
        debugState_.maskCompatible = true;
        debugState_.requested = true;
    }

    void SubmitMask(const RENDER3D::RenderTextureView& texture, D3D12_CPU_DESCRIPTOR_HANDLE srvCpu, uint64_t frameIndex,
                    float strength = 0.85f, float blur = 1.0f) noexcept {
        Mask mask{};
        mask.texture = texture;
        mask.srvCpu = srvCpu;
        mask.frameIndex = frameIndex;
        mask.strength = strength;
        mask.blur = blur;
        SubmitMask(mask);
    }

    void SubmitAccent(const Mask& accent) noexcept {
        if (accent.frameIndex == 0u) {
            debugState_.accentProvided = debugState_.accentProvided || accent.texture.resource != nullptr;
            debugState_.accentCompatible = false;
            return;
        }
        if (!frameOpen_) {
            BeginFrame(accent.frameIndex);
        } else if (frameIndex_ != accent.frameIndex) {
            debugState_.accentProvided = debugState_.accentProvided || accent.texture.resource != nullptr;
            debugState_.accentCompatible = accent_.IsValid(frameIndex_);
            return;
        }
        Mask sanitized = accent;
        sanitized.strength = std::clamp(std::isfinite(sanitized.strength) ? sanitized.strength : 1.0f, 0.0f, 8.0f);
        sanitized.blur = std::clamp(std::isfinite(sanitized.blur) ? sanitized.blur : 1.0f, 0.0f, 8.0f);
        debugState_.accentProvided = debugState_.accentProvided || sanitized.texture.resource != nullptr;
        if (!sanitized.IsValid(frameIndex_)) {
            debugState_.accentCompatible = accent_.IsValid(frameIndex_);
            return;
        }
        if (accent_.IsValid(frameIndex_) && accent_.texture.resource == sanitized.texture.resource) {
            accent_.strength = (std::max)(accent_.strength, sanitized.strength);
            accent_.blur = (std::max)(accent_.blur, sanitized.blur);
        } else {
            accent_ = sanitized;
        }
        debugState_.accentCompatible = true;
        debugState_.requested = true;
    }

    void SubmitAccent(const RENDER3D::RenderTextureView& texture, D3D12_CPU_DESCRIPTOR_HANDLE srvCpu,
                      uint64_t frameIndex, float strength = 1.0f, float blur = 0.0f) noexcept {
        Mask accent{};
        accent.texture = texture;
        accent.srvCpu = srvCpu;
        accent.frameIndex = frameIndex;
        accent.strength = strength;
        accent.blur = blur;
        SubmitAccent(accent);
    }

    bool IsCurrent(uint64_t frameIndex) const noexcept {
        return frameIndex_ == frameIndex && ((sourceSubmitted_ && (count_ != 0u || cinematicGrade_.IsActive())) ||
                                             mask_.IsValid(frameIndex) || accent_.IsValid(frameIndex));
    }

    uint64_t GetFrameIndex() const noexcept { return frameIndex_; }
    uint64_t GetSourceFrameIndex() const noexcept { return sourceFrameIndex_; }
    uint32_t GetCount() const noexcept { return count_; }
    bool HasSource() const noexcept { return sourceSubmitted_ && (count_ != 0u || cinematicGrade_.IsActive()); }
    const Mask& GetMask() const noexcept { return mask_; }
    const Mask& GetAccent() const noexcept { return accent_; }
    const CinematicGrade& GetCinematicGrade() const noexcept { return cinematicGrade_; }
    const Wave& GetWave(uint32_t index) const noexcept { return waves_[(std::min)(index, kMaximumWaveCount - 1u)]; }

    void ApplyParams(CommonParams& params) const noexcept {
        params.combo = static_cast<float>(count_);
        for (uint32_t index = 0u; index < kMaximumWaveCount; ++index) {
            const Wave& wave = waves_[index];
            const uint32_t base = index * 4u;
            params.user[base + 0u] = {wave.centerUv.x, wave.centerUv.y, wave.ageNormalized, wave.active};
            params.user[base + 1u] = {wave.maximumScreenRadius, wave.ringThickness, wave.distortionStrength,
                                      wave.intensity};
            params.user[base + 2u] = {wave.color.x, wave.color.y, wave.color.z, wave.chromaticShift};
            params.user[base + 3u] = {wave.flashIntensity, wave.waveFrequency,
                                      wave.converging ? -(wave.waveAmplitude + 16.0f) : wave.waveAmplitude,
                                      wave.waveDamping + static_cast<float>(wave.waveCount) * 16.0f};
        }
        // Keep mask controls in the padding lanes so wave 3 retains its
        // full typed profile payload. Negative pad0 disables the mask;
        // non-negative pad0 carries strength and pad1 carries blur.
        params.pad0 = mask_.IsValid(frameIndex_) ? mask_.strength : -1.0f;
        params.pad1 = mask_.IsValid(frameIndex_) ? mask_.blur : 0.0f;
    }

    DebugState& MutableDebugState() noexcept { return debugState_; }
    const DebugState& GetDebugState() const noexcept { return debugState_; }

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
    Mask accent_{};
    CinematicGrade cinematicGrade_{};
    DebugState debugState_{};
};

} // namespace HIKARI::POST
