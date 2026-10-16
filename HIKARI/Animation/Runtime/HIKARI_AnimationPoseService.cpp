#include "Animation/Runtime/HIKARI_AnimationPoseService.h"

#include <utility>

namespace HIKARI::ANIMATION {
namespace {
bool EqualPose(const AnimationLocalPose& lhs, const AnimationLocalPose& rhs) {
    if (lhs.nodes.size() != rhs.nodes.size())
        return false;
    for (size_t index = 0u; index < lhs.nodes.size(); ++index) {
        const Transform3D& a = lhs.nodes[index];
        const Transform3D& b = rhs.nodes[index];
        if (a.position != b.position || a.rotation != b.rotation || a.scale != b.scale ||
            a.useExplicitMatrix != b.useExplicitMatrix ||
            (a.useExplicitMatrix && a.explicitMatrix != b.explicitMatrix)) {
            return false;
        }
    }
    return true;
}

bool EqualSnapshot(const AnimationPoseSnapshot& lhs, const AnimationPoseSnapshot& rhs) {
    return lhs.primaryClip == rhs.primaryClip && lhs.secondaryClip == rhs.secondaryClip &&
           lhs.normalizedTime == rhs.normalizedTime && lhs.motionBlendWeight == rhs.motionBlendWeight &&
           lhs.blendWeight == rhs.blendWeight && lhs.rootMotion.translation == rhs.rootMotion.translation &&
           lhs.rootMotion.rotation == rhs.rootMotion.rotation && lhs.rootMotion.valid == rhs.rootMotion.valid &&
           lhs.rootMotionSample.valid == rhs.rootMotionSample.valid &&
           (!lhs.rootMotionSample.valid || lhs.rootMotionSample.modelMatrix == rhs.rootMotionSample.modelMatrix) &&
           lhs.transitioning == rhs.transitioning && lhs.loop == rhs.loop && lhs.playbackSpeed == rhs.playbackSpeed &&
           lhs.rootMotionEnabled == rhs.rootMotionEnabled && lhs.valid == rhs.valid &&
           EqualPose(lhs.localPose, rhs.localPose);
}
} // namespace

bool AnimationPoseService::Publish(RuntimeObjectHandle object, AnimationPoseSnapshot snapshot) {
    if (!object.IsValid())
        return false;
    const uint64_t key = object.ToValue();
    auto found = poses_.find(key);
    if (found == poses_.end()) {
        snapshot.revision = 1u;
        poses_.emplace(key, std::make_shared<AnimationPoseSnapshot>(std::move(snapshot)));
        return true;
    }
    if (EqualSnapshot(*found->second, snapshot))
        return false;
    snapshot.revision = found->second->revision + 1u;
    *found->second = std::move(snapshot);
    return true;
}

std::shared_ptr<const AnimationPoseSnapshot> AnimationPoseService::Find(RuntimeObjectHandle object) const noexcept {
    const auto found = poses_.find(object.ToValue());
    return object.IsValid() && found != poses_.end() ? found->second : nullptr;
}

void AnimationPoseService::Remove(RuntimeObjectHandle object) noexcept {
    if (object.IsValid())
        poses_.erase(object.ToValue());
}

void AnimationPoseService::Clear() noexcept { poses_.clear(); }

} // namespace HIKARI::ANIMATION
