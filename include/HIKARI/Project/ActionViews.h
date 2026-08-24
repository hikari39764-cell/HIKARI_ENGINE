#pragma once

#include <cstdint>
#include <string_view>

#include "HIKARI/Project/ActionTypes.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/VfxTypes.h"

namespace HIKARI {

    struct GameProjectSdkAccess;

    class InputView {
    public:
        InputView() = default;

        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool GetAction(
            std::string_view actionId,
            GameProjectInputActionState& outState) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectInputDevice
            GetLastActiveDevice() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API uint64_t
            GetFrameIndex() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool GetPointerState(
            GameProjectPointerState& outState) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SetContextActive(
            std::string_view contextId,
            uint8_t active) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SetUiInteractionActive(
            uint8_t active) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SetGamepadVibration(
            float lowFrequency,
            float highFrequency,
            float durationSeconds) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API void
            StopGamepadVibration() const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit InputView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

    class AnimationView {
    public:
        AnimationView() = default;

        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool Play(
            uint64_t objectHandle,
            std::string_view clipName,
            uint8_t loop = 1u,
            uint8_t restart = 1u) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool CrossFade(
            uint64_t objectHandle,
            std::string_view clipName,
            float blendSeconds,
            uint8_t loop = 1u,
            uint8_t restart = 1u) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool PlayOnce(
            uint64_t objectHandle,
            std::string_view clipName,
            uint8_t restart = 1u) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Pause(
            uint64_t objectHandle) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Resume(
            uint64_t objectHandle) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Stop(
            uint64_t objectHandle) const noexcept;
        // Sets the authored clip playback multiplier. Values outside the
        // supported positive range are rejected rather than clamped.
        HIKARI_GAME_PROJECT_SDK_API bool SetSpeed(
            uint64_t objectHandle,
            float speed) const noexcept;
        // Applies one animation pose to a model-node subtree while the base
        // Animator keeps advancing. Names are consumed synchronously and the
        // layer remains owned by the target Animator until cleared.
        HIKARI_GAME_PROJECT_SDK_API bool SetMaskedPoseLayer(
            uint64_t objectHandle,
            std::string_view clipName,
            std::string_view maskRootNodeName,
            float normalizedTime,
            float targetWeight,
            float blendSeconds) const noexcept;
        // Variant for full-body-authored overlays whose mask root would
        // otherwise inherit an unrelated locomotion-parent orientation.
        HIKARI_GAME_PROJECT_SDK_API bool
            SetMaskedPoseLayerWithRootAlignment(
                uint64_t objectHandle,
                std::string_view clipName,
                std::string_view maskRootNodeName,
                float normalizedTime,
                float targetWeight,
                float blendSeconds,
                GameProjectAnimationLayerRootAlignment rootAlignment)
                const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ClearMaskedPoseLayer(
            uint64_t objectHandle,
            float blendOutSeconds) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SetRootMotionEnabled(
            uint64_t objectHandle,
            uint8_t enabled) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool GetState(
            uint64_t objectHandle,
            GameProjectAnimationState& outState) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool GetSocketTransform(
            uint64_t objectHandle,
            std::string_view socketName,
            GameProjectAnimationSocketSpace space,
            GameProjectTransform& outTransform) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ConsumeRootMotion(
            uint64_t objectHandle,
            uint64_t fixedTickIndex,
            GameProjectRootMotionDelta& outDelta) const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit AnimationView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

    class MotionView {
    public:
        MotionView() = default;

        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool SubmitKinematicMove(
            const GameProjectKinematicMove& move) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool
            SubmitKinematicMoveWithCollisionOptions(
                const GameProjectKinematicMove& move,
                const GameProjectKinematicCollisionOptions& options)
                const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitMotionDelta(
            const GameProjectKinematicMotionDelta& delta) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool RequestTeleport(
            const GameProjectKinematicTeleport& teleport) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool GetKinematicState(
            uint64_t objectHandle,
            GameProjectKinematicState& outState) const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit MotionView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

    class PhysicsQueryView {
    public:
        PhysicsQueryView() = default;

        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool Raycast(
            const GameProjectPhysicsRaycastQuery& query,
            GameProjectPhysicsHit& outHit) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ShapeCast(
            const GameProjectPhysicsShapeCastQuery& query,
            GameProjectPhysicsHit& outHit) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API uint32_t Overlap(
            const GameProjectPhysicsOverlapQuery& query,
            GameProjectPhysicsHit* outHits,
            uint32_t capacity) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API uint32_t ReadTriggerEvents(
            GameProjectPhysicsTriggerEvent* outEvents,
            uint32_t capacity) const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit PhysicsQueryView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

    class CameraView {
    public:
        CameraView() = default;

        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool SubmitModifier(
            const GameProjectCameraModifier& modifier) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitRollModifier(
            const GameProjectCameraRollModifier& modifier) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool GetControlPose(
            GameProjectCameraPose& outPose) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitTargetFraming(
            const GameProjectCameraTargetFramingRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitTargetFramingWithDynamics(
            const GameProjectCameraTargetFramingRequest& request,
            const GameProjectCameraTargetFramingDynamics& dynamics)
            const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit CameraView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

    class TimeDilationView {
    public:
        TimeDilationView() = default;

        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API uint64_t Request(
            const GameProjectTimeDilationRequest& request) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Release(
            uint64_t token) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API float
            GetEffectiveScale() const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit TimeDilationView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

    class PresentationVfxView {
    public:
        PresentationVfxView() = default;

        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool PlayWorldEffect(
            const GameProjectCombatWorldEffectRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitWeaponTrail(
            const GameProjectWeaponTrailSample& sample) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool TriggerPerfectGuardWave(
            const GameProjectPerfectGuardWaveRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitActionCinematicGrade(
            const GameProjectActionCinematicGradeRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitActionBoostGrade(
            const GameProjectActionBoostGradeRequest& request)
            const noexcept;
        // Profiled methods use all-new request layouts so older callers keep
        // the exact legacy ABI.  The profile GUID/output id is resolved by
        // the engine's compiled HVFX registry.
        HIKARI_GAME_PROJECT_SDK_API bool SubmitWeaponTrailProfiled(
            const GameProjectWeaponTrailProfiledRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool TriggerPerfectGuardWaveProfiled(
            const GameProjectPerfectGuardWaveProfiledRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool CaptureAfterimageProfiled(
            const GameProjectAfterimageProfiledRequest& request)
            const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit PresentationVfxView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

} // namespace HIKARI
