#include "Scene/HIKARI_PresentationTransformService.h"
#include "Core/Numeric/HIKARI_NonZero.h"
#include <cstring>

namespace HIKARI {

bool PresentationTransformService::SetWorldMatrix(RuntimeObjectHandle object, const MATH::Mat4& worldMatrix) {
    if (!object.IsValid()) {
        return false;
    }
    const auto [entry, inserted] = entries_.try_emplace(object.ToValue());
    // 同一の表示姿勢では revision を進めず、休眠中の描画同期を抑える。
    if (!inserted && std::memcmp(&entry->second.worldMatrix, &worldMatrix, sizeof(worldMatrix)) == 0)
        return false;
    entry->second.worldMatrix = worldMatrix;
    NUMERIC::AdvanceNonZero(revision_);
    return true;
}

bool PresentationTransformService::Remove(RuntimeObjectHandle object) noexcept {
    if (object.IsValid() && entries_.erase(object.ToValue()) > 0u) {
        NUMERIC::AdvanceNonZero(revision_);
        return true;
    }
    return false;
}

void PresentationTransformService::Clear() noexcept {
    if (entries_.empty()) {
        return;
    }
    entries_.clear();
    NUMERIC::AdvanceNonZero(revision_);
}

bool PresentationTransformService::TryGetWorldMatrix(RuntimeObjectHandle object,
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

uint64_t PresentationTransformService::GetRevision() const noexcept { return revision_; }

} // namespace HIKARI
