#pragma once

#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "RenderContracts/Model/HIKARI_AnimationPose.h"

namespace HIKARI {
class RuntimeObjectActivityService;
namespace ANIMATION {
class AnimationNodeOverrideService;
class AnimationPoseService;
} // namespace ANIMATION

class AnimationSystem final : public ISystem {
public:
    std::string_view GetName() const override { return "AnimationSystem"; }

    void OnWorldAttached(World& world) override;
    void OnWorldDetached(World& world) override;
    void Update(World& world, const FrameContext& frame) override;

private:
    ANIMATION::AnimationPoseService* poseService_ = nullptr;
    ANIMATION::AnimationNodeOverrideService* nodeOverrideService_ = nullptr;
    const RuntimeObjectActivityService* runtimeActivityService_ = nullptr;
    std::vector<RuntimeObjectHandle> publishedObjects_{};
    ANIMATION::AnimationLocalPose secondaryPose_, transitionPose_, blendedPose_;
};

} // namespace HIKARI
