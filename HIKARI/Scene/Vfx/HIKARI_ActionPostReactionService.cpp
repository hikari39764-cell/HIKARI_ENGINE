#include "Scene/Vfx/HIKARI_ActionPostReactionService.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Scene/Vfx/HIKARI_VfxSpecialOutputBridge.h"

namespace HIKARI::VFX {
    namespace {
        constexpr float kMinimumDurationSeconds = 0.03f;
        constexpr float kMaximumDurationSeconds = 2.0f;
        constexpr float kMaximumTickSeconds = 0.25f;
        constexpr float kDefaultDurationSeconds = 0.52f;
        constexpr float kDefaultMaximumScreenRadius = 0.46f;
        constexpr float kDefaultRingThickness = 0.055f;
        constexpr float kDefaultDistortionStrength = 0.020f;
        constexpr float kDefaultChromaticShift = 0.0035f;
        constexpr float kDefaultFlashIntensity = 0.42f;

        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }

        float ClampFinite(
            float value,
            float fallback,
            float minimum,
            float maximum) noexcept {
            return std::clamp(
                FiniteOr(value, fallback), minimum, maximum);
        }

        float ActionScale(
            float requestValue,
            float defaultValue) noexcept {
            if (!std::isfinite(requestValue) ||
                !std::isfinite(defaultValue) ||
                defaultValue <= 0.0f) {
                return 1.0f;
            }
            return std::clamp(
                requestValue / defaultValue,
                0.0f,
                8.0f);
        }

        MATH::Vec3 ClampColor(const MATH::Vec3& value) noexcept {
            return {
                ClampFinite(value.x, 0.32f, 0.0f, 8.0f),
                ClampFinite(value.y, 0.78f, 0.0f, 8.0f),
                ClampFinite(value.z, 1.0f, 0.0f, 8.0f),
            };
        }

        MATH::Vec3 SanitizePosition(const MATH::Vec3& value) noexcept {
            return {
                ClampFinite(value.x, 0.0f, -100000.0f, 100000.0f),
                ClampFinite(value.y, 0.0f, -100000.0f, 100000.0f),
                ClampFinite(value.z, 0.0f, -100000.0f, 100000.0f),
            };
        }
    }

    bool ActionPostReactionService::IsDuplicate(
        const PerfectGuardShockwaveRequest& request,
        const DedupRecord& record) noexcept {

        if (!record.valid || record.sourceId != request.sourceId) {
            return false;
        }
        if (request.sequence != 0u) {
            return record.sequence == request.sequence;
        }
        return request.frameIndex != 0u &&
            record.sequence == 0u &&
            record.frameIndex == request.frameIndex;
    }

    RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrame
        ActionPostReactionService::Sanitize(
            const PerfectGuardShockwaveRequest& request) noexcept {

        RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrame result{};
        result.worldPosition = SanitizePosition(request.worldPosition);
        result.durationSeconds = ClampFinite(
            request.durationSeconds,
            kDefaultDurationSeconds,
            kMinimumDurationSeconds,
            kMaximumDurationSeconds);
        result.maximumScreenRadius = ClampFinite(
            request.maximumScreenRadius,
            kDefaultMaximumScreenRadius,
            0.02f,
            1.5f);
        result.ringThickness = ClampFinite(
            request.ringThickness,
            kDefaultRingThickness,
            0.001f,
            0.35f);
        result.distortionStrength = ClampFinite(
            request.distortionStrength,
            kDefaultDistortionStrength,
            0.0f,
            0.25f);
        result.intensity = ClampFinite(
            request.intensity,
            1.0f,
            0.0f,
            8.0f);
        result.color = ClampColor(request.color);
        result.chromaticShift = ClampFinite(
            request.chromaticShift,
            kDefaultChromaticShift,
            0.0f,
            0.05f);
        result.flashIntensity = ClampFinite(
            request.flashIntensity,
            kDefaultFlashIntensity,
            0.0f,
            4.0f);
        result.waveFrequency = ClampFinite(
            request.waveFrequency,
            1.0f,
            0.05f,
            8.0f);
        result.waveAmplitude = ClampFinite(
            request.waveAmplitude,
            1.0f,
            0.0f,
            8.0f);
        result.waveDamping = ClampFinite(
            request.waveDamping,
            1.0f,
            0.05f,
            8.0f);
        result.waveCount = std::clamp(request.waveCount, 1u, 4u);
        result.ageSeconds = 0.0f;
        return result;
    }

    void ActionPostReactionService::Remember(
        const PerfectGuardShockwaveRequest& request) noexcept {

        DedupRecord& record = dedupRecords_[nextDedupRecord_];
        record.sourceId = request.sourceId;
        record.sequence = request.sequence;
        record.frameIndex = request.frameIndex;
        record.valid = true;
        nextDedupRecord_ =
            (nextDedupRecord_ + 1u) % dedupRecords_.size();
    }

    bool ActionPostReactionService::TriggerPerfectGuardShockwave(
        const PerfectGuardShockwaveRequest& request) noexcept {

        if (request.sourceId == 0u ||
            !std::isfinite(request.worldPosition.x) ||
            !std::isfinite(request.worldPosition.y) ||
            !std::isfinite(request.worldPosition.z)) {
            return false;
        }
        for (const DedupRecord& record : dedupRecords_) {
            if (IsDuplicate(request, record)) {
                return false;
            }
        }

        size_t slot = 0u;
        bool foundInactive = false;
        float oldestAge = -1.0f;
        for (size_t index = 0u; index < activeWaves_.size(); ++index) {
            if (!activeWaves_[index].active) {
                slot = index;
                foundInactive = true;
                break;
            }
            if (activeWaves_[index].frame.ageSeconds > oldestAge) {
                oldestAge = activeWaves_[index].frame.ageSeconds;
                slot = index;
            }
        }

        activeWaves_[slot].frame = Sanitize(request);
        activeWaves_[slot].active = true;
        (void)foundInactive;
        Remember(request);
        if (request.frameIndex != 0u) {
            frameIndex_ = request.frameIndex;
        }
        RebuildFrameSource();
        try {
            std::ostringstream stream;
            stream << "[CombatVfx][PerfectGuard] wave accepted"
                << " frame=" << request.frameIndex
                << " duration=" << activeWaves_[slot].frame.durationSeconds
                << " radius=" << activeWaves_[slot].frame.maximumScreenRadius
                << " distortion=" << activeWaves_[slot].frame.distortionStrength;
            DEBUGLOG::WriteRenderLogLine(stream.str());
        }
        catch (...) {
        }
        return true;
    }

    void ActionPostReactionService::Tick(
        float unscaledDeltaSeconds,
        uint64_t frameIndex) noexcept {

        if (hasTicked_ && lastTickFrame_ == frameIndex) {
            return;
        }
        hasTicked_ = true;
        lastTickFrame_ = frameIndex;
        frameIndex_ = frameIndex;
        const float delta = std::clamp(
            FiniteOr(unscaledDeltaSeconds, 0.0f),
            0.0f,
            kMaximumTickSeconds);
        for (ActiveWave& wave : activeWaves_) {
            if (!wave.active) {
                continue;
            }
            wave.frame.ageSeconds = std::min(
                wave.frame.durationSeconds,
                wave.frame.ageSeconds + delta);
            if (wave.frame.ageSeconds >= wave.frame.durationSeconds) {
                wave.active = false;
            }
        }
        RebuildFrameSource();
    }

    size_t ActionPostReactionService::GetActiveCount() const noexcept {
        size_t count = 0u;
        for (const ActiveWave& wave : activeWaves_) {
            count += wave.active ? 1u : 0u;
        }
        return count;
    }

    void ActionPostReactionService::RebuildFrameSource() noexcept {
        frameSource_ = {};
        frameSource_.frameIndex = frameIndex_;
        for (const ActiveWave& active : activeWaves_) {
            if (!active.active ||
                frameSource_.count >= frameSource_.waves.size()) {
                continue;
            }
            frameSource_.waves[frameSource_.count++] = active.frame;
        }
    }

    bool ActionPostReactionService::TriggerPerfectGuardShockwave(
        const PerfectGuardShockwaveRequest& request,
        const VfxSpecialOutputData& output) noexcept {

        if (!output.enabled ||
            output.type != VfxSpecialOutputType::ScreenImpulse) {
            return false;
        }
        const VfxScreenImpulseProfile& profile = output.screenImpulse;
        PerfectGuardShockwaveRequest configured = request;
        // Request fields are action-level multipliers normalized against the
        // legacy defaults. The authored HVFX profile remains the predictable
        // art baseline while callers can independently scale each dimension.
        const float durationScale = ActionScale(
            request.durationSeconds,
            kDefaultDurationSeconds);
        const float radiusScale = ActionScale(
            request.maximumScreenRadius,
            kDefaultMaximumScreenRadius);
        const float thicknessScale = ActionScale(
            request.ringThickness,
            kDefaultRingThickness);
        const float distortionScale = ActionScale(
            request.distortionStrength,
            kDefaultDistortionStrength);
        const float chromaticScale = ActionScale(
            request.chromaticShift,
            kDefaultChromaticShift);
        const float flashScale = ActionScale(
            request.flashIntensity,
            kDefaultFlashIntensity);
        const float intensityScale = ActionScale(request.intensity, 1.0f);
        const float colorAlpha = std::clamp(
            std::isfinite(profile.color.w) ? profile.color.w : 1.0f,
            0.0f,
            1.0f);
        configured.durationSeconds =
            profile.durationSeconds * durationScale;
        configured.maximumScreenRadius =
            profile.maxRadius * radiusScale;
        configured.ringThickness =
            profile.ringThickness * thicknessScale;
        configured.distortionStrength =
            profile.distortion * distortionScale;
        configured.chromaticShift =
            profile.chromaticAberration * chromaticScale;
        // Profile color alpha is an authored visibility multiplier for the
        // existing RGB-only post frame contract; it scales flash and wave
        // intensity rather than becoming an ignored Inspector field.
        configured.flashIntensity = profile.flash * flashScale * colorAlpha;
        configured.intensity = profile.intensity * intensityScale * colorAlpha;
        configured.color = {
            profile.color.x,
            profile.color.y,
            profile.color.z };
        configured.waveFrequency = profile.waveFrequency;
        configured.waveAmplitude = profile.waveAmplitude;
        configured.waveDamping = profile.waveDamping;
        configured.waveCount = profile.waveCount;
        return TriggerPerfectGuardShockwave(configured);
    }

    bool ActionPostReactionService::TriggerPerfectGuardShockwave(
        const PerfectGuardShockwaveRequest& request,
        const VfxScreenImpulseProfile& profile) noexcept {

        VfxSpecialOutputData output{};
        output.type = VfxSpecialOutputType::ScreenImpulse;
        output.screenImpulse = profile;
        return TriggerPerfectGuardShockwave(request, output);
    }

    bool ActionPostReactionService::TriggerPerfectGuardShockwave(
        const PerfectGuardShockwaveRequest& request,
        const VfxProgram& program) noexcept {

        return TriggerPerfectGuardShockwave(
            request,
            program,
            {});
    }

    bool ActionPostReactionService::TriggerPerfectGuardShockwave(
        const PerfectGuardShockwaveRequest& request,
        const VfxProgram& program,
        std::string_view outputId) noexcept {

        const VfxSpecialOutputData* output = FindVfxSpecialOutput(
            program,
            outputId,
            VfxSpecialOutputType::ScreenImpulse);
        if (output == nullptr) {
            // Only the id-less overload retains compatibility fallback. A
            // profiled request with an explicit id fails closed when the
            // compiled profile is absent or has the wrong type.
            return outputId.empty()
                ? TriggerPerfectGuardShockwave(request)
                : false;
        }
        return TriggerPerfectGuardShockwave(request, *output);
    }

    void ActionPostReactionService::Reset() noexcept {
        activeWaves_ = {};
        dedupRecords_ = {};
        nextDedupRecord_ = 0u;
        frameIndex_ = 0u;
        lastTickFrame_ = 0u;
        hasTicked_ = false;
        frameSource_ = {};
    }

} // namespace HIKARI::VFX
