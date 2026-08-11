#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::EDITOR {
    namespace {

        void CollectSubtreeIds(
            const VFX::VfxAssetData& asset,
            std::string_view rootId,
            std::unordered_set<std::string>& outIds) {

            outIds.insert(std::string(rootId));
            bool expanded = true;
            while (expanded) {
                expanded = false;
                for (const VFX::VfxEmitterData& emitter : asset.emitters) {
                    if (!emitter.parentEmitterId.empty() &&
                        outIds.contains(emitter.parentEmitterId) &&
                        outIds.insert(emitter.id).second) {
                        expanded = true;
                    }
                }
            }
        }

        bool IsDescendantOf(
            const VFX::VfxAssetData& asset,
            std::string_view candidateParent,
            std::string_view target) {

            std::string current{ target };
            for (int guard = 0; guard < 1024; ++guard) {
                const VFX::VfxEmitterData* emitter = nullptr;
                for (const VFX::VfxEmitterData& candidate : asset.emitters) {
                    if (candidate.id == current) {
                        emitter = &candidate;
                        break;
                    }
                }
                if (emitter == nullptr ||
                    emitter->parentEmitterId.empty()) {
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

    bool VfxWorkspaceController::ReparentEmitter(
        std::string_view emitterId,
        std::string_view newParentEmitterId,
        std::string& outMessage) {

        if (!document_.IsOpen()) {
            outMessage = "No VFX document is open";
            return false;
        }
        if (FindEmitter(emitterId) == nullptr) {
            outMessage = "VFX emitter was not found";
            return false;
        }
        if (!newParentEmitterId.empty() &&
            FindEmitter(newParentEmitterId) == nullptr) {
            outMessage = "Parent emitter was not found";
            return false;
        }
        if (emitterId == newParentEmitterId ||
            (!newParentEmitterId.empty() &&
                IsDescendantOf(
                    document_.Asset(), emitterId, newParentEmitterId))) {
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
        moved->order = static_cast<uint32_t>(std::count_if(
            next.emitters.begin(), next.emitters.end(),
            [newParentEmitterId](const VFX::VfxEmitterData& candidate) {
                return candidate.parentEmitterId == newParentEmitterId;
            }));
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        outMessage = "VFX emitter reparented";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::ReorderEmitter(
        std::string_view emitterId,
        int direction,
        std::string& outMessage) {

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
            if (candidate.parentEmitterId != parentId ||
                candidate.id == emitterId) {
                continue;
            }
            const bool isBefore = direction < 0 &&
                candidate.order < order;
            const bool isAfter = direction > 0 &&
                candidate.order > order;
            if (!isBefore && !isAfter) continue;
            if (swapTarget == nullptr ||
                (isBefore && candidate.order > swapTarget->order) ||
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
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        outMessage = "VFX emitter reordered";
        statusMessage_ = outMessage;
        return true;
    }

    void VfxWorkspaceController::SetEmitterHidden(
        std::string_view emitterId,
        bool hidden) noexcept {

        if (hidden) {
            hiddenEmitterIds_.insert(std::string(emitterId));
        } else {
            hiddenEmitterIds_.erase(std::string(emitterId));
        }
        NotifyEmitterVisibilityChanged();
    }

    bool VfxWorkspaceController::IsEmitterHidden(
        std::string_view emitterId) const noexcept {

        return hiddenEmitterIds_.contains(std::string(emitterId));
    }

    void VfxWorkspaceController::SetSoloEmitter(
        std::string_view emitterId) noexcept {

        soloEmitterId_ = std::string(emitterId);
        NotifyEmitterVisibilityChanged();
    }

    void VfxWorkspaceController::ClearSoloEmitter() noexcept {
        soloEmitterId_.clear();
        NotifyEmitterVisibilityChanged();
    }

    const std::string& VfxWorkspaceController::GetSoloEmitterId()
        const noexcept {
        return soloEmitterId_;
    }

    bool VfxWorkspaceController::IsEmitterPreviewVisible(
        std::string_view emitterId) const noexcept {

        if (!document_.IsOpen()) return false;
        const VFX::VfxAssetData& asset = document_.Asset();

        if (!soloEmitterId_.empty()) {
            std::unordered_set<std::string> soloTree{};
            CollectSubtreeIds(asset, soloEmitterId_, soloTree);
            if (!soloTree.contains(std::string(emitterId))) {
                return false;
            }
        }

        std::string current{ emitterId };
        for (int guard = 0; guard < 1024 && !current.empty(); ++guard) {
            if (hiddenEmitterIds_.contains(current)) return false;
            const VFX::VfxEmitterData* emitter = nullptr;
            for (const VFX::VfxEmitterData& candidate : asset.emitters) {
                if (candidate.id == current) {
                    emitter = &candidate;
                    break;
                }
            }
            if (emitter == nullptr) break;
            current = emitter->parentEmitterId;
        }
        return true;
    }

    bool VfxWorkspaceController::FocusSelectedEmitter(
        std::string& outMessage) {

        if (selection_.emitterId.empty() ||
            FindEmitter(selection_.emitterId) == nullptr) {
            outMessage = "Select an emitter to focus";
            return false;
        }
        if (!FocusPreviewAsset(selection_.emitterId)) {
            outMessage = "Selected emitter has no previewable bounds";
            statusMessage_ = outMessage;
            return false;
        }
        outMessage = "Preview camera focused on emitter";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::StepPreviewFrame(
        int frameCount,
        std::string& outMessage) {

        if (!document_.IsOpen()) {
            outMessage = "No VFX document is open";
            return false;
        }
        const int steps = (std::max)(1, (std::min)(frameCount, 120));
        const float stepSeconds = 1.0f / 60.0f;
        if (!previewPlaying_ || !previewHandle_.IsValid()) {
            if (!PlayPreview(outMessage)) {
                return false;
            }
            previewPaused_ = true;
            outMessage =
                "Preview initial frame submitted; step after GPU consumption";
            statusMessage_ = outMessage;
            return false;
        }
        if (previewRenderMailbox_.HasPending()) {
            outMessage = "Preview is waiting for the current GPU frame";
            statusMessage_ = outMessage;
            return false;
        }
        if (previewRestartPending_) {
            if (!RestartPreview(outMessage)) {
                return false;
            }
            previewPaused_ = true;
            outMessage =
                "Preview restart frame submitted; step after GPU consumption";
            statusMessage_ = outMessage;
            return false;
        }
        const float playbackEnd = previewLoopEnabled_
            ? previewLoopEndSeconds_
            : GetPreviewTimelineDuration();
        const float target = (std::min)(
            previewElapsedSeconds_ +
                static_cast<float>(steps) * stepSeconds,
            playbackEnd);
        if (target <= previewElapsedSeconds_ + 1.0e-6f) {
            outMessage = "Preview is at the end of its playback range";
            statusMessage_ = outMessage;
            previewPaused_ = true;
            return false;
        }
        if (!SeekActivePreview(target, outMessage)) {
            statusMessage_ = outMessage;
            previewPaused_ = true;
            return false;
        }
        previewPaused_ = true;
        outMessage = "Preview stepped " + std::to_string(steps) +
            " frame(s)";
        statusMessage_ = outMessage;
        return true;
    }

    void VfxWorkspaceController::NotifyEmitterStructureChanged() noexcept {
        if (document_.IsOpen()) {
            std::erase_if(
                hiddenEmitterIds_,
                [this](const std::string& id) {
                    return FindEmitter(id) == nullptr;
                });
            if (!soloEmitterId_.empty() &&
                FindEmitter(soloEmitterId_) == nullptr) {
                soloEmitterId_.clear();
            }
        } else {
            hiddenEmitterIds_.clear();
            soloEmitterId_.clear();
        }
        NotifyEmitterVisibilityChanged();
    }

    void VfxWorkspaceController::NotifyEmitterVisibilityChanged() noexcept {
        if (document_.IsOpen()) {
            (void)PublishPreviewRenderInput(
                VFX::VfxRenderInputPublishMode::SupersedePending);
        }
    }

} // namespace HIKARI::EDITOR
