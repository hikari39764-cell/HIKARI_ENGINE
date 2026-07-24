#pragma once

#include <vector>

#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequenceTrackDriver.h"

namespace HIKARI {

    class Camera3D;
    class World;

namespace SEQUENCER {

    class CameraSequenceTrackDriver final : public ISequenceTrackDriver {
    public:
        CameraSequenceTrackDriver(
            CameraDirector& cameraDirector,
            const World& world,
            const Camera3D& viewportCamera) noexcept;

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

        CameraDirector& cameraDirector_;
        const World& world_;
        const Camera3D& viewportCamera_;
        std::vector<InstanceState> states_{};
    };

} // namespace SEQUENCER
} // namespace HIKARI
