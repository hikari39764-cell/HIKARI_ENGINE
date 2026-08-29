#include "Animation/Runtime/HIKARI_AnimationNodeOverrideService.h"

#include <algorithm>
#include <cmath>

#include "Animation/Runtime/HIKARI_AnimationPoseEvaluation.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::ANIMATION {
namespace {

    bool IsFinite(const Transform3D& value) noexcept {
        return MATH::IsFinite(value.position) &&
            MATH::IsFinite(value.rotation) &&
            MATH::IsFinite(value.scale) &&
            value.scale.x > 0.0f &&
            value.scale.y > 0.0f &&
            value.scale.z > 0.0f;
    }

} // namespace

    bool AnimationNodeOverrideService::SetLocalOffset(
        RuntimeObjectHandle object,
        std::string_view nodeName,
        const Transform3D& offset) {
        return SetOffset(
            object,
            nodeName,
            offset,
            OffsetSpace::NodeLocal);
    }

    bool AnimationNodeOverrideService::SetParentLocalOffset(
        RuntimeObjectHandle object,
        std::string_view nodeName,
        const Transform3D& offset) {
        return SetOffset(
            object,
            nodeName,
            offset,
            OffsetSpace::ParentLocal);
    }

    bool AnimationNodeOverrideService::SetOffset(
        RuntimeObjectHandle object,
        std::string_view nodeName,
        const Transform3D& offset,
        OffsetSpace space) {
        constexpr size_t kMaximumOverridesPerObject = 16u;
        if (!object.IsValid() || nodeName.empty() ||
            nodeName.size() > 255u || !IsFinite(offset)) {
            return false;
        }
        std::vector<Entry>& entries = entries_[object.ToValue()];
        const auto found = std::find_if(
            entries.begin(),
            entries.end(),
            [nodeName](const Entry& entry) {
                return entry.nodeName == nodeName;
            });
        if (found != entries.end()) {
            found->offset = offset;
            found->space = space;
            return true;
        }
        if (entries.size() >= kMaximumOverridesPerObject) {
            return false;
        }
        entries.push_back(Entry{
            std::string(nodeName), offset, space });
        return true;
    }

    bool AnimationNodeOverrideService::ClearLocalOffset(
        RuntimeObjectHandle object,
        std::string_view nodeName) noexcept {
        if (!object.IsValid() || nodeName.empty()) {
            return false;
        }
        const auto bucket = entries_.find(object.ToValue());
        if (bucket == entries_.end()) {
            return false;
        }
        std::vector<Entry>& entries = bucket->second;
        const size_t previousSize = entries.size();
        std::erase_if(entries, [nodeName](const Entry& entry) {
            return entry.nodeName == nodeName;
        });
        const bool changed = entries.size() != previousSize;
        if (entries.empty()) {
            entries_.erase(bucket);
        }
        return changed;
    }

    void AnimationNodeOverrideService::Remove(
        RuntimeObjectHandle object) noexcept {
        if (object.IsValid()) {
            entries_.erase(object.ToValue());
        }
    }

    void AnimationNodeOverrideService::Clear() noexcept {
        entries_.clear();
    }

    bool AnimationNodeOverrideService::Apply(
        const ModelAsset& model,
        RuntimeObjectHandle object,
        AnimationLocalPose& inOutPose) const {
        if (!object.IsValid() ||
            !inOutPose.IsValidFor(model.nodes.size())) {
            return false;
        }
        const auto bucket = entries_.find(object.ToValue());
        if (bucket == entries_.end()) {
            return true;
        }
        bool valid = true;
        for (const Entry& entry : bucket->second) {
            int nodeIndex = -1;
            if (!POSE_EVALUATION::ResolveNodeByExactName(
                    model, entry.nodeName, nodeIndex) ||
                nodeIndex < 0 ||
                static_cast<size_t>(nodeIndex) >= inOutPose.nodes.size()) {
                valid = false;
                continue;
            }
            Transform3D& node = inOutPose.nodes[
                static_cast<size_t>(nodeIndex)];
            const MATH::Mat4 nodeLocal = node.GetLocalMatrix();
            const MATH::Mat4 offset = entry.offset.GetLocalMatrix();
            // Parent-local composition is deliberately pre-multiplied. It is
            // independent of an animated node scale used for visibility, so
            // an attachment correction cannot grow or collapse during a
            // weapon draw transition.
            const MATH::Mat4 composed =
                entry.space == OffsetSpace::ParentLocal
                ? offset * nodeLocal
                : nodeLocal * offset;
            node = {};
            node.useExplicitMatrix = true;
            node.explicitMatrix = composed;
        }
        return valid;
    }

} // namespace HIKARI::ANIMATION
