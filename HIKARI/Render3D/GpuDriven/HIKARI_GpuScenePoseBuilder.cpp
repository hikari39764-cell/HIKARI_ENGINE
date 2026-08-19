#include "Render3D/GpuDriven/HIKARI_GpuScenePoseBuilder.h"

#include "Animation/Runtime/HIKARI_AnimationPoseEvaluation.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
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
        (void)ANIMATION::POSE_EVALUATION::BuildNodeGlobalMatrices(
            *record.model,
            pose,
            entry.localNodeGlobals);

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
