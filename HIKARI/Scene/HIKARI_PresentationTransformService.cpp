#include "Scene/HIKARI_PresentationTransformService.h"

namespace HIKARI {

    void PresentationTransformService::SetWorldMatrix(
        RuntimeObjectHandle object,
        const MATH::Mat4& worldMatrix) {
        if (!object.IsValid()) {
            return;
        }
        entries_[object.ToValue()].worldMatrix = worldMatrix;
        AdvanceRevision();
    }

    bool PresentationTransformService::Remove(
        RuntimeObjectHandle object) noexcept {
        if (object.IsValid() &&
            entries_.erase(object.ToValue()) > 0u) {
            AdvanceRevision();
            return true;
        }
        return false;
    }

    void PresentationTransformService::Clear() noexcept {
        if (entries_.empty()) {
            return;
        }
        entries_.clear();
        AdvanceRevision();
    }

    bool PresentationTransformService::TryGetWorldMatrix(
        RuntimeObjectHandle object,
        MATH::Mat4& outWorldMatrix) const noexcept {
        if (!object.IsValid()) {
            return false;
        }
        const auto found = entries_.find(object.ToValue());
        if (found == entries_.end()) {
            return false;
        }
        outWorldMatrix = found->second.worldMatrix;
        return true;
    }

    uint64_t PresentationTransformService::GetRevision() const noexcept {
        return revision_;
    }

    void PresentationTransformService::AdvanceRevision() noexcept {
        ++revision_;
        if (revision_ == 0u) {
            revision_ = 1u;
        }
    }

} // namespace HIKARI
