#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "Animation/Runtime/HIKARI_AnimationPose.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::ANIMATION {

    // Animation evaluation owns the conversion from sampled local node data
    // to model-space matrices.  Render3D has a similar internal path for its
    // cooked render model, but must not become an animation data owner.
    namespace POSE_EVALUATION {

        inline MATH::Mat4 GetNodeLocalMatrix(
            const ModelAsset& model,
            size_t nodeIndex,
            const AnimationLocalPose* pose = nullptr) noexcept {
            if (nodeIndex >= model.nodes.size()) {
                return MATH::Mat4::Identity();
            }
            const ModelNode& node = model.nodes[nodeIndex];
            if (node.hasLocalMatrix) {
                return node.localMatrix;
            }
            if (pose != nullptr && nodeIndex < pose->nodes.size()) {
                return pose->nodes[nodeIndex].GetLocalMatrix();
            }
            return node.localTransform.GetLocalMatrix();
        }

        inline bool BuildNodeGlobalMatricesRecursive(
            const ModelAsset& model,
            const AnimationLocalPose* pose,
            int nodeIndex,
            std::vector<MATH::Mat4>& globals,
            std::vector<uint8_t>& state) noexcept {
            if (nodeIndex < 0 ||
                nodeIndex >= static_cast<int>(model.nodes.size())) {
                return false;
            }
            const size_t index = static_cast<size_t>(nodeIndex);
            if (state[index] == 2u) {
                return true;
            }
            // Malformed imported assets can contain a parent cycle.  Do not
            // recurse forever or publish a partially composed matrix.
            if (state[index] == 1u) {
                return false;
            }
            state[index] = 1u;

            MATH::Mat4 parent = MATH::Mat4::Identity();
            const int parentIndex = model.nodes[index].parent;
            if (parentIndex >= 0) {
                if (!BuildNodeGlobalMatricesRecursive(
                        model,
                        pose,
                        parentIndex,
                        globals,
                        state)) {
                    state[index] = 0u;
                    return false;
                }
                parent = globals[static_cast<size_t>(parentIndex)];
            }

            globals[index] = parent * GetNodeLocalMatrix(model, index, pose);
            state[index] = 2u;
            return true;
        }

        inline bool BuildNodeGlobalMatrices(
            const ModelAsset& model,
            const AnimationLocalPose* pose,
            std::vector<MATH::Mat4>& outGlobals) {
            outGlobals.assign(
                model.nodes.size(),
                MATH::Mat4::Identity());
            std::vector<uint8_t> state(model.nodes.size(), 0u);
            bool valid = true;
            for (size_t index = 0u; index < model.nodes.size(); ++index) {
                if (!BuildNodeGlobalMatricesRecursive(
                        model,
                        pose,
                        static_cast<int>(index),
                        outGlobals,
                        state)) {
                    valid = false;
                }
            }
            return valid;
        }

        inline std::vector<MATH::Mat4> BuildNodeGlobalMatrices(
            const ModelAsset& model,
            const AnimationLocalPose* pose = nullptr) {
            std::vector<MATH::Mat4> result{};
            (void)BuildNodeGlobalMatrices(model, pose, result);
            return result;
        }

        inline bool IsValidNodeIndex(
            const ModelAsset& model,
            int nodeIndex) noexcept {
            return nodeIndex >= 0 &&
                nodeIndex < static_cast<int>(model.nodes.size());
        }

        inline int ResolveRootNodeFromSkin(
            const ModelAsset& model,
            const SkeletonAsset& skin) {
            if (IsValidNodeIndex(model, skin.skeletonRootNode)) {
                return skin.skeletonRootNode;
            }
            if (skin.joints.empty()) {
                return -1;
            }

            // Walk the node parent chain while the ancestor remains part of
            // the skin's joint set.  This is more robust than trusting the
            // compact parentJoint field when old/hand-authored assets left it
            // at -1 for every joint.
            std::vector<uint8_t> isJoint(model.nodes.size(), 0u);
            for (const SkeletonJoint& joint : skin.joints) {
                if (IsValidNodeIndex(model, joint.nodeIndex)) {
                    isJoint[static_cast<size_t>(joint.nodeIndex)] = 1u;
                }
            }
            for (const SkeletonJoint& joint : skin.joints) {
                if (!IsValidNodeIndex(model, joint.nodeIndex)) {
                    continue;
                }
                int candidate = joint.nodeIndex;
                std::vector<uint8_t> visited(model.nodes.size(), 0u);
                while (IsValidNodeIndex(model, candidate) &&
                    !visited[static_cast<size_t>(candidate)]) {
                    visited[static_cast<size_t>(candidate)] = 1u;
                    const int parent = model.nodes[
                        static_cast<size_t>(candidate)].parent;
                    if (!IsValidNodeIndex(model, parent) ||
                        !isJoint[static_cast<size_t>(parent)]) {
                        break;
                    }
                    candidate = parent;
                }
                return candidate;
            }

            // Imported data normally has at least one compact top-level link;
            // retain that fallback for malformed node parent chains.
            for (const SkeletonJoint& joint : skin.joints) {
                if (IsValidNodeIndex(model, joint.nodeIndex) &&
                    joint.parentJoint < 0) {
                    return joint.nodeIndex;
                }
            }
            return -1;
        }

        inline int ResolveRootNode(
            const ModelAsset& model) {
            // A declared skeletonRootNode wins globally, even when another
            // skin in the asset only has inferable compact joints.
            for (const SkeletonAsset& skin : model.skins) {
                if (IsValidNodeIndex(model, skin.skeletonRootNode)) {
                    return skin.skeletonRootNode;
                }
            }
            for (const SkeletonAsset& skin : model.skins) {
                const int root = ResolveRootNodeFromSkin(model, skin);
                if (IsValidNodeIndex(model, root)) {
                    return root;
                }
            }
            // Unskinned animation can still have a meaningful root.  Only
            // use a genuine node hierarchy root; never use scene-root
            // metadata for skeletal/root-motion selection.
            for (size_t index = 0u; index < model.nodes.size(); ++index) {
                if (model.nodes[index].parent < 0) {
                    return static_cast<int>(index);
                }
            }
            return -1;
        }

        inline bool ResolveNodeByExactName(
            const ModelAsset& model,
            std::string_view name,
            int& outNodeIndex) noexcept {
            outNodeIndex = -1;
            if (name.empty()) {
                return false;
            }
            for (size_t index = 0u; index < model.nodes.size(); ++index) {
                if (model.nodes[index].name == name) {
                    outNodeIndex = static_cast<int>(index);
                    return true;
                }
            }
            return false;
        }

        inline bool DecomposeTransform(
            const MATH::Mat4& matrix,
            Transform3D& outTransform) noexcept {
            MATH::Vec3 translation{};
            MATH::Quat rotation = MATH::Quat::Identity();
            MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
            const bool decomposed = MATH::DecomposeTRS(
                matrix,
                translation,
                rotation,
                scale);
            if (!decomposed ||
                !MATH::IsFinite(translation) ||
                !MATH::IsFinite(rotation) ||
                !MATH::IsFinite(scale)) {
                outTransform = {};
                outTransform.useExplicitMatrix = true;
                outTransform.explicitMatrix = matrix;
                return false;
            }
            outTransform = {};
            outTransform.position = translation;
            outTransform.rotation = MATH::NormalizeQ(rotation);
            outTransform.scale = scale;
            // Retain the exact composed matrix for consumers that need to
            // preserve non-TRS data while still exposing decomposed values.
            outTransform.useExplicitMatrix = true;
            outTransform.explicitMatrix = matrix;
            return true;
        }

        inline bool EvaluateRootMotion(
            const ModelAsset& model,
            const AnimationPoseSnapshot* previous,
            const AnimationPoseSnapshot& current,
            AnimationRootMotionDelta& outDelta) {
            outDelta = {};
            if (previous == nullptr ||
                !previous->valid ||
                !current.valid ||
                !previous->rootMotionSample.valid ||
                !current.rootMotionSample.valid ||
                !IsValidNodeIndex(model, ResolveRootNode(model))) {
                return false;
            }

            // Re-baseline once when a new destination clip/transition starts.
            // Subsequent frames use the final blended pose, so root motion
            // remains continuous through the blend instead of losing the
            // complete cross-fade interval.
            if (previous->primaryClip != current.primaryClip ||
                previous->secondaryClip != current.secondaryClip) {
                return false;
            }

            const float previousTime = previous->normalizedTime;
            const float currentTime = current.normalizedTime;
            if (std::isfinite(previousTime) &&
                std::isfinite(currentTime)) {
                const bool movingBackward = current.playbackSpeed < 0.0f;
                const bool directionChanged =
                    (previous->playbackSpeed < 0.0f) != movingBackward;
                const bool discontinuity = directionChanged ||
                    (current.loop != previous->loop) ||
                    (movingBackward
                        ? currentTime > previousTime + 1.0e-4f
                        : currentTime + 1.0e-4f < previousTime);
                if (discontinuity) {
                    // This covers loop wrap and non-loop restart/seek.  The
                    // next frame starts from the new root baseline.
                    return false;
                }
            }

            // CaptureRootMotionSample removes the bind transform first, so
            // this delta is expressed in character/model axes rather than in
            // a coordinate-conversion joint's local frame. The gameplay owner
            // can map it into world space with the actor transform.
            const MATH::Mat4 delta =
                MATH::Inverse(previous->rootMotionSample.modelMatrix) *
                current.rootMotionSample.modelMatrix;
            MATH::Vec3 translation{};
            MATH::Quat rotation = MATH::Quat::Identity();
            MATH::Vec3 scale{};
            if (!MATH::DecomposeTRS(
                    delta,
                    translation,
                    rotation,
                    scale) ||
                !MATH::IsFinite(translation) ||
                !MATH::IsFinite(rotation)) {
                return false;
            }
            outDelta.translation = translation;
            outDelta.rotation = MATH::NormalizeQ(rotation);
            outDelta.valid = true;
            return true;
        }

        inline bool CaptureRootMotionSample(
            const ModelAsset& model,
            const AnimationLocalPose& pose,
            AnimationRootMotionSample& outSample) {
            outSample = {};
            if (!pose.IsValidFor(model.nodes.size())) {
                return false;
            }
            const int rootNode = ResolveRootNode(model);
            if (!IsValidNodeIndex(model, rootNode)) {
                return false;
            }
            std::vector<MATH::Mat4> posedGlobals{};
            std::vector<MATH::Mat4> bindGlobals{};
            if (!BuildNodeGlobalMatrices(model, &pose, posedGlobals) ||
                !BuildNodeGlobalMatrices(model, nullptr, bindGlobals) ||
                static_cast<size_t>(rootNode) >= posedGlobals.size() ||
                static_cast<size_t>(rootNode) >= bindGlobals.size()) {
                return false;
            }

            MATH::Vec3 bindTranslation{};
            MATH::Quat bindRotation = MATH::Quat::Identity();
            MATH::Vec3 bindScale{};
            const MATH::Mat4& bindMatrix =
                bindGlobals[static_cast<size_t>(rootNode)];
            if (!MATH::DecomposeTRS(
                    bindMatrix,
                    bindTranslation,
                    bindRotation,
                    bindScale) ||
                std::abs(bindScale.x) <= 1.0e-6f ||
                std::abs(bindScale.y) <= 1.0e-6f ||
                std::abs(bindScale.z) <= 1.0e-6f) {
                return false;
            }

            // M(t) = Motion(t) * Bind. Store Motion(t), not the raw joint
            // matrix. This prevents a bind-space axis conversion (Summer's
            // root is rotated -90 degrees around X) from turning forward root
            // translation into vertical physics motion.
            outSample.modelMatrix =
                posedGlobals[static_cast<size_t>(rootNode)] *
                MATH::Inverse(bindMatrix);
            outSample.valid = true;
            return true;
        }

        inline bool StripRootMotionFromPose(
            const ModelAsset& model,
            AnimationLocalPose& pose) {
            if (!pose.IsValidFor(model.nodes.size())) {
                return false;
            }
            const int rootNode = ResolveRootNode(model);
            if (!IsValidNodeIndex(model, rootNode)) {
                return false;
            }
            pose.nodes[static_cast<size_t>(rootNode)] =
                model.nodes[static_cast<size_t>(rootNode)].localTransform;
            return true;
        }

    } // namespace POSE_EVALUATION

    // Stable aliases keep call sites readable without exposing the internal
    // namespace as a second ownership boundary.
    inline std::vector<MATH::Mat4> BuildAnimationNodeGlobalMatrices(
        const ModelAsset& model,
        const AnimationLocalPose* pose = nullptr) {
        return POSE_EVALUATION::BuildNodeGlobalMatrices(model, pose);
    }

    inline int ResolveAnimationRootNode(
        const ModelAsset& model) {
        return POSE_EVALUATION::ResolveRootNode(model);
    }

    inline bool EvaluateAnimationRootMotion(
        const ModelAsset& model,
        const AnimationPoseSnapshot* previous,
        const AnimationPoseSnapshot& current,
        AnimationRootMotionDelta& outDelta) {
        return POSE_EVALUATION::EvaluateRootMotion(
            model,
            previous,
            current,
            outDelta);
    }

} // namespace HIKARI::ANIMATION
