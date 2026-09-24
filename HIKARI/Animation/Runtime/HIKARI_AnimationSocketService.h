#pragma once

#include <string_view>
#include <vector>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Animation/Runtime/HIKARI_AnimationPoseEvaluation.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI::ANIMATION {

enum class AnimationSocketSpace {
    Local,
    World,
};

// Runtime socket lookup is deliberately a narrow, stateless service. It
// consumes a model and the final evaluated pose, while the GameObject
// remains authoritative for the object/world transform.
class AnimationSocketService {
public:
    bool Resolve(const ModelAsset& model, const AnimationPoseSnapshot* pose, const MATH::Mat4& authoritativeObjectWorld,
                 std::string_view socketName, AnimationSocketSpace space, Transform3D& outTransform) const {
        outTransform = {};
        int nodeIndex = -1;
        if (!POSE_EVALUATION::ResolveNodeByExactName(model, socketName, nodeIndex)) {
            return false;
        }

        AnimationLocalPose bindPose{};
        const AnimationLocalPose* effectivePose = pose != nullptr ? &pose->localPose : nullptr;
        if (effectivePose == nullptr || !effectivePose->IsValidFor(model.nodes.size())) {
            BuildBindPose(model, bindPose);
            effectivePose = &bindPose;
        }

        std::vector<MATH::Mat4> globals{};
        if (!POSE_EVALUATION::BuildNodeGlobalMatrices(model, effectivePose, globals) ||
            static_cast<size_t>(nodeIndex) >= globals.size()) {
            return false;
        }
        // "Local" means model/object local space at the public boundary,
        // not the node's immediate parent space.
        MATH::Mat4 socketMatrix = globals[static_cast<size_t>(nodeIndex)];
        if (space == AnimationSocketSpace::World) {
            // The object transform is authoritative.  Do not read or
            // write a presentation transform and do not mutate the
            // GameObject merely to answer a socket query.
            socketMatrix = authoritativeObjectWorld * globals[static_cast<size_t>(nodeIndex)];
        }
        return POSE_EVALUATION::DecomposeTransform(socketMatrix, outTransform);
    }

    bool Resolve(const ModelAsset& model, const AnimationPoseSnapshot& pose, const MATH::Mat4& authoritativeObjectWorld,
                 std::string_view socketName, AnimationSocketSpace space, Transform3D& outTransform) const {
        return Resolve(model, &pose, authoritativeObjectWorld, socketName, space, outTransform);
    }

    bool Resolve(const GameObject& object, const ModelAsset& model, const AnimationPoseSnapshot* pose,
                 std::string_view socketName, AnimationSocketSpace space, Transform3D& outTransform) const {
        return Resolve(model, pose, object.GetTransform().GetWorldMatrix(), socketName, space, outTransform);
    }

    bool Resolve(const GameObject& object, const ModelAsset& model, const AnimationPoseSnapshot& pose,
                 std::string_view socketName, AnimationSocketSpace space, Transform3D& outTransform) const {
        return Resolve(object, model, &pose, socketName, space, outTransform);
    }

    bool ResolveLocal(const ModelAsset& model, const AnimationPoseSnapshot* pose, std::string_view socketName,
                      Transform3D& outTransform) const {
        return Resolve(model, pose, MATH::Mat4::Identity(), socketName, AnimationSocketSpace::Local, outTransform);
    }

    bool ResolveWorld(const ModelAsset& model, const AnimationPoseSnapshot* pose,
                      const MATH::Mat4& authoritativeObjectWorld, std::string_view socketName,
                      Transform3D& outTransform) const {
        return Resolve(model, pose, authoritativeObjectWorld, socketName, AnimationSocketSpace::World, outTransform);
    }

    bool GetSocketTransform(const ModelAsset& model, const AnimationPoseSnapshot* pose,
                            const MATH::Mat4& authoritativeObjectWorld, std::string_view socketName,
                            AnimationSocketSpace space, Transform3D& outTransform) const {
        return Resolve(model, pose, authoritativeObjectWorld, socketName, space, outTransform);
    }

    bool GetSocketTransform(const GameObject& object, const ModelAsset& model, const AnimationPoseSnapshot* pose,
                            std::string_view socketName, AnimationSocketSpace space, Transform3D& outTransform) const {
        return Resolve(object, model, pose, socketName, space, outTransform);
    }
};

} // namespace HIKARI::ANIMATION
