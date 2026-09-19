#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Render3D/Vfx/HIKARI_ActionPostReactionFrameSource.h"

namespace HIKARI::VFX {

    struct VfxProgram;

    // Scene-owned, presentation-time combat reactions. It owns no GPU
    // resources and never changes gameplay or time scale.
    class ActionPostReactionService final {
    public:
        struct PerfectGuardShockwaveRequest {
            uint64_t sourceId = 0u;
            uint64_t sequence = 0u;
            uint64_t frameIndex = 0u;
            MATH::Vec3 worldPosition{};
            float durationSeconds = 0.52f;
            float maximumScreenRadius = 0.46f;
            float ringThickness = 0.055f;
            float distortionStrength = 0.020f;
            float intensity = 1.0f;
            MATH::Vec3 color{ 0.32f, 0.78f, 1.0f };
            float chromaticShift = 0.0035f;
            float flashIntensity = 0.42f;
            float waveFrequency = 1.0f;
            float waveAmplitude = 1.0f;
            float waveDamping = 1.0f;
            uint32_t waveCount = 1u;
            uint32_t flags = 0u;
        };

        struct ActionCinematicGradeRequest {
            uint64_t sourceId = 0u;
            uint64_t frameIndex = 0u;
            int32_t priority = 0;
            float weight = 0.0f;
            float monochromeStrength = 0.0f;
            float chromaticSeparationPixels = 0.0f;
            float contrastStrength = 0.0f;
        };

        struct ActionBoostGradeRequest {
            uint64_t sourceId = 0u;
            uint64_t frameIndex = 0u;
            int32_t priority = 0;
            float weight = 0.0f;
            float edgeIntensity = 0.0f;
            float edgeWidth = 0.20f;
            float chromaticSeparationPixels = 0.0f;
        };

        static constexpr uint32_t kMaximumWaveCount =
            RENDER3D::COMBAT_VFX::kMaximumPerfectGuardShockwaves;

        ActionPostReactionService() = default;
        ~ActionPostReactionService() = default;

        ActionPostReactionService(
            const ActionPostReactionService&) = delete;
        ActionPostReactionService& operator=(
            const ActionPostReactionService&) = delete;

        // Returns false for invalid values or a duplicate source/sequence
        // (or source/frame when sequence is unavailable).
        bool TriggerPerfectGuardShockwave(
            const PerfectGuardShockwaveRequest& request) noexcept;
        bool TriggerPerfectGuardShockwave(
            const PerfectGuardShockwaveRequest& request,
            const VfxSpecialOutputData& output) noexcept;
        bool TriggerPerfectGuardShockwave(
            const PerfectGuardShockwaveRequest& request,
            const VfxScreenImpulseProfile& profile) noexcept;
        bool TriggerPerfectGuardShockwave(
            const PerfectGuardShockwaveRequest& request,
            const VfxProgram& program) noexcept;
        bool TriggerPerfectGuardShockwave(
            const PerfectGuardShockwaveRequest& request,
            const VfxProgram& program,
            std::string_view outputId) noexcept;

        // VFX 再生側から毎フレーム差し替える画面波。ゲームプレイ由来の波と
        // 同じウェーブ表を共有し、エフェクト側の年齢はエフェクト内時刻で
        // 決まる（Tick で加算しない）。Tick 後に投稿し、投稿しないフレームでは消える。
        void SubmitVfxScreenWaves(
            std::span<const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrame>
                waves) noexcept;

        // Frame-local arbitration for full-screen action grading. Callers
        // submit every visible frame; an omitted frame clears the grade.
        bool SubmitActionCinematicGrade(
            const ActionCinematicGradeRequest& request) noexcept;
        bool SubmitActionBoostGrade(
            const ActionBoostGradeRequest& request) noexcept;

        // Uses unscaled presentation time so hit-stop cannot freeze the
        // screen reaction. Delta is bounded to keep a stalled frame finite.
        void Tick(float unscaledDeltaSeconds, uint64_t frameIndex) noexcept;

        const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource&
            GetFrameSource() const noexcept {
            return frameSource_;
        }

        size_t GetActiveCount() const noexcept;
        void Reset() noexcept;

    private:
        struct ActiveWave {
            RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrame frame{};
            bool active = false;
        };

        struct DedupRecord {
            uint64_t sourceId = 0u;
            uint64_t sequence = 0u;
            uint64_t frameIndex = 0u;
            bool valid = false;
        };

        static constexpr size_t kDedupRecordCount = 16u;

        static bool IsDuplicate(
            const PerfectGuardShockwaveRequest& request,
            const DedupRecord& record) noexcept;
        static RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrame Sanitize(
            const PerfectGuardShockwaveRequest& request) noexcept;
        void RebuildFrameSource() noexcept;
        void Remember(const PerfectGuardShockwaveRequest& request) noexcept;
        static RENDER3D::COMBAT_VFX::ActionCinematicGradeFrame
            SanitizeGrade(
                const ActionCinematicGradeRequest& request) noexcept;
        static bool IsPreferredGrade(
            const ActionCinematicGradeRequest& candidate,
            const ActionCinematicGradeRequest& current) noexcept;
        static RENDER3D::COMBAT_VFX::ActionBoostGradeFrame
            SanitizeBoostGrade(
                const ActionBoostGradeRequest& request) noexcept;
        static bool IsPreferredBoostGrade(
            const ActionBoostGradeRequest& candidate,
            const ActionBoostGradeRequest& current) noexcept;

        std::array<ActiveWave, kMaximumWaveCount> activeWaves_{};
        // VFX 再生が毎フレーム差し替える画面波。ゲームプレイ側の
        // activeWaves_ とは寿命の決め方が違うので別に保持する。
        std::array<RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrame,
            kMaximumWaveCount> vfxWaves_{};
        size_t vfxWaveCount_ = 0u;
        std::array<DedupRecord, kDedupRecordCount> dedupRecords_{};
        size_t nextDedupRecord_ = 0u;
        uint64_t frameIndex_ = 0u;
        uint64_t lastTickFrame_ = 0u;
        bool hasTicked_ = false;
        ActionCinematicGradeRequest gradeRequest_{};
        bool gradeSubmitted_ = false;
        ActionBoostGradeRequest boostGradeRequest_{};
        bool boostGradeSubmitted_ = false;
        RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource frameSource_{};
    };

} // namespace HIKARI::VFX
