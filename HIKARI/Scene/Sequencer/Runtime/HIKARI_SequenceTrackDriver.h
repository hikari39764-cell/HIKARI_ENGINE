#pragma once

#include <string_view>
#include <vector>

#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackTypes.h"

namespace HIKARI::SEQUENCER {

    struct SequencePlaybackInstanceView {
        SequencePlaybackHandle handle{};
        const SequenceAsset* asset = nullptr;
        const SequenceBindingContext* bindings = nullptr;
        std::string_view channel{};
        int priority = 0;
    };

    class ISequenceTrackDriver {
    public:
        virtual ~ISequenceTrackDriver() = default;

        virtual std::string_view GetDriverId() const noexcept = 0;
        virtual void OnSequenceStarted(
            const SequencePlaybackInstanceView& instance) = 0;
        virtual void Validate(
            const SequencePlaybackInstanceView& instance,
            std::vector<SequencePlaybackDiagnostic>& diagnostics) const {
            (void)instance;
            (void)diagnostics;
        }
        virtual void Evaluate(
            const SequencePlaybackInstanceView& instance,
            const SequenceEvaluationContext& context) = 0;
        virtual void OnSequenceStopped(
            SequencePlaybackHandle handle,
            SequenceStopReason reason) = 0;
        virtual void Reset() = 0;
    };

} // namespace HIKARI::SEQUENCER
