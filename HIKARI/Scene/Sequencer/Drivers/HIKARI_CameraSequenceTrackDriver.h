#pragma once

#include <vector>
#include "Scene/Sequencer/Runtime/HIKARI_SequencePresentationState.h"

#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequenceTrackDriver.h"

namespace HIKARI {

    class Camera3D;
    class World;

namespace SEQUENCER {

    // 時間評価は純粋な読み取り。実行時とバックグラウンドプレビューで共有する。
    bool EvaluateSequenceCameraView(const CinematicSequence& sequence,
        const SequenceBindingContext& bindings, const World& world, const CameraDirector& director,
        float seconds, float aspect, CinematicCameraEvaluation& evaluation, Camera3D& out);

    class CameraSequenceTrackDriver final : public ISequenceTrackDriver {
    public:
        CameraSequenceTrackDriver(
            CameraDirector& cameraDirector,
            const World& world,
            const Camera3D& viewportCamera,
            SequencePresentationState* presentation = nullptr) noexcept;

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
        struct InstanceState {
            SequencePlaybackHandle handle{};
            CameraOverrideToken overrideToken{};
            SceneObjectId cameraObjectId{};
            uint64_t shotId = 0;
        };

        InstanceState* FindState(
            SequencePlaybackHandle handle) noexcept;
        void ReleaseOverride(InstanceState& state) noexcept;

        SequencePresentationState* presentation_ = nullptr;
        CameraDirector& cameraDirector_;
        const World& world_;
        const Camera3D& viewportCamera_;
        std::vector<InstanceState> states_{};
    };

} // namespace SEQUENCER
} // namespace HIKARI
