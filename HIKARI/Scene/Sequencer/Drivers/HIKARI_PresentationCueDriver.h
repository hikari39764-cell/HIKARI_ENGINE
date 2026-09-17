#pragma once

#include "Scene/Sequencer/Runtime/HIKARI_SequenceTrackDriver.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackTypes.h"

namespace HIKARI { class World; }
namespace HIKARI::VFX { class VfxPlaybackService; }

namespace HIKARI::SEQUENCER {
    class PresentationCueDriver final : public ISequenceTrackDriver {
    public:
        PresentationCueDriver(World& world, VFX::VfxPlaybackService& vfx);
        std::string_view GetDriverId() const noexcept override { return "PresentationCue"; }
        void OnSequenceStarted(const SequencePlaybackInstanceView& instance) override;
        void Validate(const SequencePlaybackInstanceView& instance,
            std::vector<SequencePlaybackDiagnostic>& diagnostics) const override;
        void Evaluate(const SequencePlaybackInstanceView& instance,
            const SequenceEvaluationContext& context) override;
        void OnSequenceStopped(SequencePlaybackHandle handle, SequenceStopReason reason) override;
        void Reset() override;
    private:
        struct CueState {
            int sound = -1;
            int voice = -1;
            VFX::VfxPlaybackHandle effect{};
            bool entered = false;
            bool warned = false;
        };
        struct InstanceState {
            SequencePlaybackHandle handle{};
            std::vector<CueState> cues;
        };
        void StopCue(CueState& cue);
        bool ResolveTransform(const SequencePlaybackInstanceView& instance,
            SequenceBindingId binding, VFX::VfxWorldTransform& out) const;
        World& world_;
        VFX::VfxPlaybackService& vfx_;
        std::vector<InstanceState> states_;
    };
}
