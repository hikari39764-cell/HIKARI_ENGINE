#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Render3D/Vfx/HIKARI_ActionPostReactionFrameSource.h"

namespace HIKARI::VFX {

    struct VfxProgram;

    // Scene-owned, presentation-time reaction state for perfect guard only.
    // It owns no GPU resources and never changes gameplay or time scale.
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

        std::array<ActiveWave, kMaximumWaveCount> activeWaves_{};
        std::array<DedupRecord, kDedupRecordCount> dedupRecords_{};
        size_t nextDedupRecord_ = 0u;
        uint64_t frameIndex_ = 0u;
        uint64_t lastTickFrame_ = 0u;
        bool hasTicked_ = false;
        RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource frameSource_{};
    };

} // namespace HIKARI::VFX
