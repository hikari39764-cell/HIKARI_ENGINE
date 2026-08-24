#pragma once

#include <string_view>
#include <vector>

#include "Animation/Runtime/HIKARI_AnimationPose.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequenceTrackDriver.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackTypes.h"

namespace HIKARI {

    class AnimatorComponent;
    class World;

    namespace SERVICES {
        class ActionTimeDilationService;
    }

    namespace VFX {
        class VfxPlaybackService;
    }

    namespace SEQUENCER {

        class PresentationSequenceTrackDriver final
            : public ISequenceTrackDriver {
        public:
            PresentationSequenceTrackDriver(
                World& world,
                VFX::VfxPlaybackService& vfxPlayback,
                SERVICES::ActionTimeDilationService& timeDilation) noexcept;

            std::string_view GetDriverId() const noexcept override;
            void OnSequenceStarted(
                const SequencePlaybackInstanceView& instance) override;
            void Validate(
                const SequencePlaybackInstanceView& instance,
                std::vector<SequencePlaybackDiagnostic>& diagnostics)
                const override;
            void Evaluate(
                const SequencePlaybackInstanceView& instance,
                const SequenceEvaluationContext& context) override;
            void OnSequenceStopped(
                SequencePlaybackHandle handle,
                SequenceStopReason reason) override;
            void Reset() override;

        private:
            struct AnimatorSnapshot {
                SceneObjectId objectId{};
                ANIMATION::AnimationMotionSample motion{};
                float timeSeconds = 0.0f;
                float speed = 1.0f;
                bool loop = true;
                bool autoPlay = true;
                bool rootMotionEnabled = false;
                bool playing = false;
                uint64_t activeClipId = 0;
            };

            struct InstanceState {
                SequencePlaybackHandle handle{};
                std::vector<AnimatorSnapshot> animators{};
                std::vector<VFX::VfxPlaybackHandle> vfxHandles{};
                uint64_t timeDilationToken = 0;
                uint64_t timeDilationClipId = 0;
                float timeDilationScale = 1.0f;
                int32_t timeDilationPriority = 0;
            };

            InstanceState& EnsureState(SequencePlaybackHandle handle);
            InstanceState* FindState(
                SequencePlaybackHandle handle) noexcept;
            AnimatorSnapshot& CaptureAnimator(
                InstanceState& state,
                SceneObjectId objectId,
                AnimatorComponent& animator);
            void RestoreAnimator(AnimatorSnapshot& snapshot) noexcept;
            void RestoreInactiveAnimators(
                InstanceState& state,
                const std::vector<SceneObjectId>& activeObjects);
            void ReleaseState(InstanceState& state) noexcept;
            void EvaluateAnimation(
                InstanceState& state,
                const SequencePlaybackInstanceView& instance,
                const SequenceEvaluationContext& context);
            void EvaluateVfx(
                InstanceState& state,
                const SequencePlaybackInstanceView& instance,
                const SequenceEvaluationContext& context);
            void EvaluateTimeDilation(
                InstanceState& state,
                const SequencePlaybackInstanceView& instance,
                const SequenceEvaluationContext& context);
            void EvaluateSignals(
                const SequencePlaybackInstanceView& instance,
                const SequenceEvaluationContext& context);

            World& world_;
            VFX::VfxPlaybackService& vfxPlayback_;
            SERVICES::ActionTimeDilationService& timeDilation_;
            std::vector<InstanceState> states_{};
        };

    } // namespace SEQUENCER
} // namespace HIKARI
