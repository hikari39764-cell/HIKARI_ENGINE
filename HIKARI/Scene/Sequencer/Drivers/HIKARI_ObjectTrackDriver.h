#pragma once

#include <string>
#include <vector>

#include "Core/Math/HIKARI_Transform3D.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequenceTrackDriver.h"

namespace HIKARI {
class World;
}

namespace HIKARI::SEQUENCER {
// 物体轨を動かす。初めて書く前に物体の局所の姿勢（と灯の明るさ）を覚え、演出が止まったら戻す。
// 戻す時、こちらが最後に書いた姿勢から誰か（作者の編集や別の仕組み）が変えていれば、その物体には触らない。
// 镜头が物体に結ばれていても同じフレームで追えるよう、镜头の駆動より先に登録する。
class ObjectTrackDriver final : public ISequenceTrackDriver {
public:
    explicit ObjectTrackDriver(World& world);
    std::string_view GetDriverId() const noexcept override { return "ObjectTrack"; }
    void OnSequenceStarted(const SequencePlaybackInstanceView& instance) override;
    void Validate(const SequencePlaybackInstanceView& instance,
                  std::vector<SequencePlaybackDiagnostic>& diagnostics) const override;
    void Evaluate(const SequencePlaybackInstanceView& instance, const SequenceEvaluationContext& context) override;
    void OnSequenceStopped(SequencePlaybackHandle handle, SequenceStopReason reason) override;
    void Reset() override;

private:
    struct Touched {
        SceneObjectId object{};
        Transform3D original{};
        Transform3D written{};
        bool hasLight = false;
        float originalLight = 0.0f;
        float writtenLight = 0.0f;
        bool restore = true;
    };
    struct InstanceState {
        SequencePlaybackHandle handle{};
        bool preview = false;
        std::vector<Touched> touched;
    };
    void Restore(InstanceState& state);
    World& world_;
    std::vector<InstanceState> states_;
};
} // namespace HIKARI::SEQUENCER
