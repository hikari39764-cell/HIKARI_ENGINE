#include "Render3D/GpuDriven/HIKARI_GpuScenePoseBuilder.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        MATH::Mat4 ResolveNodeLocalMatrix(
            const ModelNode& node,
            const ANIMATION::AnimationLocalPose& pose,
            size_t nodeIndex) {

            if (node.hasLocalMatrix) {
                return node.localMatrix;
            }
            if (nodeIndex < pose.nodes.size()) {
                return pose.nodes[nodeIndex].GetLocalMatrix();
            }
            return node.localTransform.GetLocalMatrix();
        }

        void EvaluateNodeMatrixRecursive(
            const ModelAsset& model,
            const ANIMATION::AnimationLocalPose& pose,
            int nodeIndex,
            const MATH::Mat4& parentWorld,
            std::vector<MATH::Mat4>& outGlobals,
            std::vector<uint8_t>& visited) {

            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
                return;
            }
            const size_t index = static_cast<size_t>(nodeIndex);
            if (visited[index]) {
                return;
            }

            const ModelNode& node = model.nodes[index];
            outGlobals[index] =
                parentWorld * ResolveNodeLocalMatrix(node, pose, index);
            visited[index] = 1u;

            for (int childIndex : node.children) {
                EvaluateNodeMatrixRecursive(
                    model,
                    pose,
                    childIndex,
                    outGlobals[index],
                    outGlobals,
                    visited);
            }
        }

        void BuildNodeGlobalMatricesWithRoot(
            const ModelAsset& model,
            const ANIMATION::AnimationLocalPose& pose,
            const MATH::Mat4& rootWorld,
            std::vector<MATH::Mat4>& outGlobals,
            std::vector<uint8_t>& visited) {

            if (model.nodes.empty()) {
                outGlobals.clear();
                visited.clear();
                return;
            }

            outGlobals.assign(model.nodes.size(), rootWorld);
            visited.assign(model.nodes.size(), 0u);

            for (size_t i = 0; i < model.nodes.size(); ++i) {
                if (model.nodes[i].parent == -1) {
                    EvaluateNodeMatrixRecursive(
                        model,
                        pose,
                        static_cast<int>(i),
                        rootWorld,
                        outGlobals,
                        visited);
                }
            }
            for (size_t i = 0; i < model.nodes.size(); ++i) {
                if (visited[i]) {
                    continue;
                }
                const int parent = model.nodes[i].parent;
                const MATH::Mat4 parentWorld =
                    parent >= 0 && parent < static_cast<int>(outGlobals.size())
                        ? outGlobals[static_cast<size_t>(parent)]
                        : rootWorld;
                EvaluateNodeMatrixRecursive(
                    model,
                    pose,
                    static_cast<int>(i),
                    parentWorld,
                    outGlobals,
                    visited);
            }
        }

        bool BuildJointPalette(
            const ModelAsset& model,
            int skinIndex,
            const std::vector<MATH::Mat4>& localNodeGlobals,
            std::vector<MATH::Mat4>& outPalette) {

            outPalette.clear();
            const SkeletonAsset* skin = model.FindSkin(skinIndex);
            if (skin == nullptr) {
                return false;
            }

            outPalette.resize(skin->joints.size(), MATH::Mat4::Identity());
            for (size_t jointIndex = 0; jointIndex < skin->joints.size(); ++jointIndex) {
                const SkeletonJoint& joint = skin->joints[jointIndex];
                if (joint.nodeIndex < 0 ||
                    joint.nodeIndex >= static_cast<int>(localNodeGlobals.size())) {
                    continue;
                }
                outPalette[jointIndex] =
                    localNodeGlobals[static_cast<size_t>(joint.nodeIndex)] *
                    joint.inverseBindMatrix;
            }
            return true;
        }
    }

    GpuScenePoseBuilder::PoseEntry* GpuScenePoseBuilder::ResolvePoseEntry(
        const GpuSceneSurfaceRecord& record) {

        if (record.model == nullptr) {
            return nullptr;
        }

        for (PoseEntry& entry : entries_) {
            if (entry.objectId == record.objectId &&
                entry.objectVersion == record.objectVersion &&
                entry.model == record.model) {
                return &entry;
            }
        }

        PoseEntry entry{};
        entry.objectId = record.objectId;
        entry.objectVersion = record.objectVersion;
        entry.model = record.model;
        ANIMATION::AnimationLocalPose bindPose{};
        const ANIMATION::AnimationLocalPose* pose = nullptr;
        if (record.animationPose != nullptr &&
            record.animationPose->localPose.IsValidFor(
                record.model->nodes.size())) {
            pose = &record.animationPose->localPose;
        } else {
            bindPose.nodes.reserve(record.model->nodes.size());
            for (const ModelNode& node : record.model->nodes) {
                bindPose.nodes.push_back(node.localTransform);
            }
            pose = &bindPose;
        }
        BuildNodeGlobalMatricesWithRoot(
            *record.model,
            *pose,
            MATH::Mat4::Identity(),
            entry.localNodeGlobals,
            entry.visited);

        entries_.push_back(std::move(entry));
        return &entries_.back();
    }

    const std::vector<MATH::Mat4>* GpuScenePoseBuilder::ResolveJointPalette(
        const GpuSceneSurfaceRecord& record) {

        if (record.model == nullptr || record.surface == nullptr) {
            return nullptr;
        }
        const int skinIndex = record.surface->skinIndex;
        if (skinIndex < 0) {
            return nullptr;
        }

        PoseEntry* entry = ResolvePoseEntry(record);
        if (entry == nullptr) {
            return nullptr;
        }

        auto found = entry->palettesBySkin.find(skinIndex);
        if (found != entry->palettesBySkin.end()) {
            return &found->second;
        }

        std::vector<MATH::Mat4>& palette = entry->palettesBySkin[skinIndex];
        if (!BuildJointPalette(
            *record.model,
            skinIndex,
            entry->localNodeGlobals,
            palette)) {
            entry->palettesBySkin.erase(skinIndex);
            return nullptr;
        }
        return &palette;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
