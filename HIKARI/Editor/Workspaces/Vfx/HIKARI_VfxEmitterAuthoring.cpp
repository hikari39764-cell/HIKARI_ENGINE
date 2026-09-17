#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"
#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace HIKARI::EDITOR {
    bool VfxWorkspaceController::AddEmitter(
        std::string_view parentEmitterId,
        std::string_view displayName,
        std::string_view requestedId,
        std::string& outId,
        std::string& outMessage,
        VFX::VfxLayerKind kind) {

        if (!document_.IsOpen()) {
            outMessage = "No VFX document is open";
            return false;
        }
        if (!parentEmitterId.empty() &&
            FindEmitter(parentEmitterId) == nullptr) {
            outMessage = "Parent emitter was not found";
            return false;
        }
        const std::string id = requestedId.empty()
            ? AllocateStableId("emitter")
            : std::string(requestedId);
        if (ContainsStableId(id)) {
            outMessage = "VFX stable ID already exists: " + id;
            return false;
        }

        VFX::VfxEmitterData emitter =
            VFX::MakeDefaultVfxAssetData().emitters.front();
        RegenerateEmitterIds(emitter);
        emitter.id = id;
        emitter.kind = kind;
        if (kind != VFX::VfxLayerKind::Particles) {
            emitter.renderShape.billboardMode = VFX::VfxBillboardMode::Fixed;
            emitter.spawn.ratePerSecond = 0;
            emitter.spawn.burstCount = 0;
            emitter.spawn.burstTimeline.clear();
            emitter.renderCommon.colorTextureAssetGuid = {};
        }
        emitter.parentEmitterId = parentEmitterId;
        emitter.name = displayName.empty()
            ? "New Layer"
            : std::string(displayName);
        emitter.order = static_cast<uint32_t>(std::count_if(
            document_.Asset().emitters.begin(),
            document_.Asset().emitters.end(),
            [parentEmitterId](const VFX::VfxEmitterData& candidate) {
                return candidate.parentEmitterId == parentEmitterId;
            }));

        VFX::VfxAssetData next = document_.Asset();
        next.emitters.push_back(std::move(emitter));
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        outId = id;
        Select({ VfxWorkspaceSelectionKind::Emitter, id, id });
        outMessage = "VFX emitter created";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::DuplicateEmitter(
        std::string_view emitterId,
        std::string& outId,
        std::string& outMessage) {

        const VFX::VfxEmitterData* source = FindEmitter(emitterId);
        if (source == nullptr) {
            outMessage = "VFX emitter was not found";
            return false;
        }
        VFX::VfxEmitterData duplicate = *source;
        RegenerateEmitterIds(duplicate);
        duplicate.name += " Copy";
        ++duplicate.order;
        const std::string id = duplicate.id;
        VFX::VfxAssetData next = document_.Asset();
        next.emitters.push_back(std::move(duplicate));
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        outId = id;
        Select({ VfxWorkspaceSelectionKind::Emitter, id, id });
        outMessage = "VFX emitter duplicated";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::RemoveEmitter(
        std::string_view emitterId,
        std::string& outMessage) {

        if (FindEmitter(emitterId) == nullptr) {
            outMessage = "VFX emitter was not found";
            return false;
        }
        if (document_.Asset().emitters.size() <= 1u) {
            outMessage = "A VFX asset must keep at least one emitter";
            return false;
        }
        VFX::VfxAssetData next = document_.Asset();
        VFX::VfxEmitterData* removed = FindEmitterMutable(next, emitterId);
        const std::string parentId = removed != nullptr
            ? removed->parentEmitterId
            : std::string{};
        for (VFX::VfxEmitterData& emitter : next.emitters) {
            if (emitter.parentEmitterId == emitterId) {
                emitter.parentEmitterId = parentId;
            }
        }
        std::erase_if(
            next.emitters,
            [emitterId](const VFX::VfxEmitterData& emitter) {
                return emitter.id == emitterId;
            });
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        selection_ = {};
        outMessage = "VFX emitter removed; child emitters were promoted";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::AddPresetLayers(std::string_view presetId, std::string& outMessage) {
        const auto* preset = VFX::PRESETS::FindVfxPreset(presetId);
        if (!document_.IsOpen() || preset == nullptr) {
            outMessage = "Open an effect and select a valid template first";
            return false;
        }
        auto package = VFX::PRESETS::MakeVfxPresetPackage(presetId);
        auto next = document_.Asset();
        std::unordered_map<std::string, std::string> inputIds, emitterIds;
        for (auto& input : package.asset.inputs) {
            const auto oldId = input.id;
            input.id = AllocateStableId("input");
            inputIds.emplace(oldId, input.id);
            next.inputs.push_back(std::move(input));
        }
        for (auto& emitter : package.asset.emitters) {
            const auto oldId = emitter.id;
            RegenerateEmitterIds(emitter);
            emitterIds.emplace(oldId, emitter.id);
            for (auto& link : emitter.inputLinks) {
                if (const auto found = inputIds.find(link.inputId); found != inputIds.end()) link.inputId = found->second;
            }
        }
        if (package.asset.emitters.empty()) {
            outMessage = "Template has no effect layers";
            return false;
        }
        const auto selectedId = package.asset.emitters.front().id;
        for (auto& emitter : package.asset.emitters) {
            if (!emitter.parentEmitterId.empty()) emitter.parentEmitterId = emitterIds.at(emitter.parentEmitterId);
            emitter.order = static_cast<uint32_t>(next.emitters.size());
            next.emitters.push_back(std::move(emitter));
        }
        // One document transaction owns the complete composition and Undo.
        document_.SealMerge();
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        document_.SealMerge();
        Select({VfxWorkspaceSelectionKind::Emitter, selectedId, selectedId});
        outMessage = "Added template layers: " + preset->displayName;
        statusMessage_ = outMessage;
        return true;
    }

}
