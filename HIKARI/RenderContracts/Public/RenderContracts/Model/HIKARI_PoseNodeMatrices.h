#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "RenderContracts/Model/HIKARI_AnimationPose.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"

namespace HIKARI::ANIMATION {

// 局所姿勢からモデル空間のノード行列を作る純関数。アニメーション評価と、描画側の姿勢パレット作りが共用する。
// ルートモーションなど評価のロジックは Animation の HIKARI_AnimationPoseEvaluation.h に残る。
namespace POSE_EVALUATION {

inline MATH::Mat4 GetNodeLocalMatrix(const ModelAsset& model, size_t nodeIndex,
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

inline bool BuildNodeGlobalMatricesRecursive(const ModelAsset& model, const AnimationLocalPose* pose, int nodeIndex,
                                             std::vector<MATH::Mat4>& globals, std::vector<uint8_t>& state) noexcept {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
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
        if (!BuildNodeGlobalMatricesRecursive(model, pose, parentIndex, globals, state)) {
            state[index] = 0u;
            return false;
        }
        parent = globals[static_cast<size_t>(parentIndex)];
    }

    globals[index] = parent * GetNodeLocalMatrix(model, index, pose);
    state[index] = 2u;
    return true;
}

inline bool BuildNodeGlobalMatrices(const ModelAsset& model, const AnimationLocalPose* pose,
                                    std::vector<MATH::Mat4>& outGlobals) {
    outGlobals.assign(model.nodes.size(), MATH::Mat4::Identity());
    std::vector<uint8_t> state(model.nodes.size(), 0u);
    bool valid = true;
    for (size_t index = 0u; index < model.nodes.size(); ++index) {
        if (!BuildNodeGlobalMatricesRecursive(model, pose, static_cast<int>(index), outGlobals, state)) {
            valid = false;
        }
    }
    return valid;
}

inline std::vector<MATH::Mat4> BuildNodeGlobalMatrices(const ModelAsset& model,
                                                       const AnimationLocalPose* pose = nullptr) {
    std::vector<MATH::Mat4> result{};
    (void)BuildNodeGlobalMatrices(model, pose, result);
    return result;
}

} // namespace POSE_EVALUATION

} // namespace HIKARI::ANIMATION
