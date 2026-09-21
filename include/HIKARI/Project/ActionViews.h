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
        // 所有スレッドから毎フレーム指定する。非表示は Play 中の有効な
        // ポインターに限り受理し、省略・フォーカス喪失時は自動で戻る。
        // UI スプライトの送信成功後に呼ぶ。座標とキャプチャは変更しない。
        HIKARI_GAME_PROJECT_SDK_API bool SetPointerCursorVisible(
            uint8_t visible) const noexcept;
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
        // Runtime animation-LOD gate. Disabling retains the last evaluated
        // pose and skips later clip sampling until re-enabled. The target
        // must own an AnimatorComponent in the current World.
        HIKARI_GAME_PROJECT_SDK_API bool SetEvaluationEnabled(
            uint64_t objectHandle,
            uint8_t enabled) const noexcept;
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
        // Applies an additive node-local offset after animation evaluation.
        // The engine owns storage and applies the same offset to rendered poses
        // and explicit gameplay socket samples until cleared or the object dies.
        HIKARI_GAME_PROJECT_SDK_API bool SetNodeLocalTransformOffset(
            uint64_t objectHandle,
            std::string_view nodeName,
            const GameProjectTransform& offset) const noexcept;
        // Applies an additive transform in the node parent's local space.
        // Unlike a node-local offset, its translation is not multiplied by
        // the node's animated scale. This is the canonical correction path
        // for hand-held nodes that animate scale for visibility.
        HIKARI_GAME_PROJECT_SDK_API bool SetNodeParentLocalTransformOffset(
            uint64_t objectHandle,
            std::string_view nodeName,
            const GameProjectTransform& offset) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ClearNodeLocalTransformOffset(
            uint64_t objectHandle,
            std::string_view nodeName) const noexcept;
        // Samples one non-looping clip at an explicit gameplay time and
        // resolves all requested sockets from that same pose. This is the
        // fixed-step combat path; it does not mutate Animator playback.
        HIKARI_GAME_PROJECT_SDK_API bool SampleSocketTransformsAtTime(
            uint64_t objectHandle,
            std::string_view clipName,
            float timeSeconds,
            uint8_t stripRootMotion,
            const std::string_view* socketNames,
            uint32_t socketCount,
            GameProjectAnimationSocketSpace space,
            GameProjectTransform* outTransforms) const noexcept;
        // Evaluates authored root displacement over one explicit non-looping
        // clip interval. Translation remains in character/model axes and the
        // caller routes it through MotionView; no Scene transform is written.
        HIKARI_GAME_PROJECT_SDK_API bool SampleRootMotionDeltaAtTime(
            uint64_t objectHandle,
            std::string_view clipName,
            float previousTimeSeconds,
            float currentTimeSeconds,
            GameProjectRootMotionDelta& outDelta) const noexcept;
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
        // ワールド更新スレッドで、直近に解決した実行時の描画カメラをコピーする。
        // カメラ未解決・実行停止時は false を返し、出力を初期化する。
        HIKARI_GAME_PROJECT_SDK_API bool GetRenderedPose(
            GameProjectCameraPose& outPose) const noexcept;
        // 同スレッドで有効な全画面の放射状歪みを取得する。歪みなしは 0、0。
        // 実行停止・描画未初期化時は false。時間変化するグリッチは含まない。
        HIKARI_GAME_PROJECT_SDK_API bool GetRadialScreenWarp(
            float& outCurvature, float& outStrength) const noexcept;
        // 提出したフレームだけ有効。停止すると入力条件／既定モードへ復帰する。
        // Scene-authored camera flow. Commands queue for this frame's camera evaluation.
        // Queries do not consume sequencer events; runtime status never writes authored scene data.
        HIKARI_GAME_PROJECT_SDK_API bool TriggerFlow(uint64_t flowObjectHandle) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool InterruptFlow(uint64_t flowObjectHandle) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool GetFlowStatus(uint64_t flowObjectHandle, GameProjectCameraFlowStatus& out) const noexcept;
        // Authored .hcamera effect. Call once; returns a playback token, or 0 on failure.
        // Duration/curves come from the asset. Requests belong to this camera and World.
        HIKARI_GAME_PROJECT_SDK_API uint64_t PlayGameplayEffect(uint64_t cameraObjectHandle,
            std::string_view effectId, float strength = 1.0f) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool StopGameplayEffect(uint64_t cameraObjectHandle,
            uint64_t playbackToken) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitGameplayMode(uint64_t cameraObjectHandle,
            std::string_view mode, uint64_t sourceId, int32_t priority = 0) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitPose(
            const GameProjectCameraPoseRequest& request) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool IsReady() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitTargetFraming(
            const GameProjectCameraTargetFramingRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitTargetFramingWithDynamics(
            const GameProjectCameraTargetFramingRequest& request,
            const GameProjectCameraTargetFramingDynamics& dynamics)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitRelationshipShot(
            const GameProjectCameraTargetFramingRequest& request,
            const GameProjectCameraTargetFramingDynamics& dynamics,
            const GameProjectCameraRelationshipShotProfile& profile)
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
        HIKARI_GAME_PROJECT_SDK_API bool SubmitGroundTelegraph(
            const GameProjectGroundTelegraphRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitOcclusionReveal(
            const GameProjectOcclusionRevealRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitWorldBarrier(
            const GameProjectWorldBarrierRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitWeaponTrail(
            const GameProjectWeaponTrailSample& sample) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool TriggerPerfectGuardWave(
            const GameProjectPerfectGuardWaveRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitPostProfileParameter(
            std::string_view profileId,
            const GameProjectPostProfileParameterRequest& request) const noexcept;
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
