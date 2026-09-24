#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::EDITOR {
namespace {

bool IsDescendantOf(const VFX::VfxAssetData& asset, std::string_view candidateParent, std::string_view target) {
    std::string current{target};
    for (int guard = 0; guard < 1024; ++guard) {
        const VFX::VfxEmitterData* emitter = nullptr;
        for (const VFX::VfxEmitterData& candidate : asset.emitters) {
            if (candidate.id == current) {
                emitter = &candidate;
                break;
            }
        }
        if (emitter == nullptr || emitter->parentEmitterId.empty()) {
            return false;
        }
        if (emitter->parentEmitterId == candidateParent) {
            return true;
        }
        current = emitter->parentEmitterId;
    }
    return false;
}

} // namespace

bool VfxWorkspaceController::ReparentEmitter(std::string_view emitterId, std::string_view newParentEmitterId,
                                             std::string& outMessage) {
    if (!document_.IsOpen()) {
        outMessage = "No VFX document is open";
        return false;
    }
    if (FindEmitter(emitterId) == nullptr) {
        outMessage = "VFX emitter was not found";
        return false;
    }
    if (!newParentEmitterId.empty() && FindEmitter(newParentEmitterId) == nullptr) {
        outMessage = "Parent emitter was not found";
        return false;
    }
    if (emitterId == newParentEmitterId ||
        (!newParentEmitterId.empty() && IsDescendantOf(document_.Asset(), emitterId, newParentEmitterId))) {
        outMessage = "Cannot parent an emitter to itself or its child";
        return false;
    }
    const VFX::VfxEmitterData* current = FindEmitter(emitterId);
    if (current->parentEmitterId == newParentEmitterId) {
        outMessage = "Emitter already has that parent";
        return false;
    }

    VFX::VfxAssetData next = document_.Asset();
    VFX::VfxEmitterData* moved = FindEmitterMutable(next, emitterId);
    moved->parentEmitterId = std::string(newParentEmitterId);
    moved->order = static_cast<uint32_t>(std::count_if(next.emitters.begin(), next.emitters.end(),
                                                       [newParentEmitterId](const VFX::VfxEmitterData& candidate) {
                                                           return candidate.parentEmitterId == newParentEmitterId;
                                                       }));
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    outMessage = "VFX emitter reparented";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::ReorderEmitter(std::string_view emitterId, int direction, std::string& outMessage) {
    if (!document_.IsOpen()) {
        outMessage = "No VFX document is open";
        return false;
    }
    const VFX::VfxEmitterData* emitter = FindEmitter(emitterId);
    if (emitter == nullptr) {
        outMessage = "VFX emitter was not found";
        return false;
    }
    const std::string parentId = emitter->parentEmitterId;
    const uint32_t order = emitter->order;

    VFX::VfxAssetData next = document_.Asset();
    VFX::VfxEmitterData* swapTarget = nullptr;
    for (VFX::VfxEmitterData& candidate : next.emitters) {
        if (candidate.parentEmitterId != parentId || candidate.id == emitterId) {
            continue;
        }
        const bool isBefore = direction < 0 && candidate.order < order;
        const bool isAfter = direction > 0 && candidate.order > order;
        if (!isBefore && !isAfter)
            continue;
        if (swapTarget == nullptr || (isBefore && candidate.order > swapTarget->order) ||
            (isAfter && candidate.order < swapTarget->order)) {
            swapTarget = &candidate;
        }
    }
    if (swapTarget == nullptr) {
        outMessage = "Emitter is already at the boundary";
        return false;
    }
    VFX::VfxEmitterData* moved = FindEmitterMutable(next, emitterId);
    std::swap(moved->order, swapTarget->order);
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    outMessage = "VFX emitter reordered";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::FocusSelectedEmitter(std::string& outMessage) {
    if (selection_.emitterId.empty() || FindEmitter(selection_.emitterId) == nullptr) {
        outMessage = "Select an emitter to focus";
        return false;
    }
    if (!preview_.FocusPreviewAsset(selection_.emitterId)) {
        outMessage = "Selected emitter has no previewable bounds";
        statusMessage_ = outMessage;
        return false;
    }
    outMessage = "Preview camera focused on emitter";
    statusMessage_ = outMessage;
    return true;
}

} // namespace HIKARI::EDITOR
