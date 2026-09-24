#pragma once
#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Scene/HIKARI_ISystem.h"
#include <vector>
namespace HIKARI {
class CameraFollowComponent;
class CameraFlowSystem final : public ISystem {
public:
    std::string_view GetName() const override { return "CameraFlowSystem"; }
    void LateUpdate(World& world, const FrameContext& frame) override;
    void OnWorldDetached(World& world) override;

private:
    void Process(World& world, GameObject& owner, CameraFlowComponent& flow, float dt, bool startAllowed);
    void Finish(World& world, CameraFlowComponent& flow, CameraFollowComponent& camera, bool completed);
    std::vector<SEQUENCER::SequencePlaybackHandle> tracked_;
};
} // namespace HIKARI
