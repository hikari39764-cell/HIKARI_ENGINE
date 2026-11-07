#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"
#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"
#include "Assets/Vfx/HIKARI_VfxGeometryPresetCatalog.h"

#include "Assets/Vfx/HIKARI_VfxLayerOrder.h"
#include <algorithm>
#include <unordered_map>
#include <utility>

namespace HIKARI::EDITOR {
bool VfxWorkspaceController::ApplyGeometryPreset(std::string_view layerId, std::string_view presetId,
                                                 std::string& outMessage) {
    const VFX::VfxGeometryPresetDescriptor* preset = VFX::FindVfxGeometryPreset(presetId);
    if (preset == nullptr) {
        outMessage = "Unknown geometry preset";
        return false;
    }
    VFX::VfxAssetData next = document_.Asset();
    VFX::VfxEmitterData* layer = FindEmitterMutable(next, layerId);
    if (layer == nullptr || layer->kind != VFX::VfxLayerKind::Shape) {
        outMessage = "Geometry presets apply to shape layers";
        return false;
    }
    // 形状設定の一括変更は 1 トランザクションで行う（undo 一回）。
    VFX::ApplyVfxGeometryPreset(*preset, layer->renderShape);
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    outMessage = "Geometry preset applied: " + std::string(preset->displayName);
    return true;
}

const VFX::VfxEmitterData* VfxWorkspaceController::FindFluidDomainUnder(
    std::string_view parentEmitterId) const noexcept {
    const VFX::VfxEmitterData* parent = parentEmitterId.empty() ? nullptr : FindEmitter(parentEmitterId);
    if (parent == nullptr || parent->kind == VFX::VfxLayerKind::Fluid)
        return parent;
    if (parent->kind != VFX::VfxLayerKind::Group)
        return nullptr;
    return VFX::FindVfxFluidDomainLayer(*parent, [&](const std::string& id) { return FindEmitter(id); });
}

bool VfxWorkspaceController::AddEmitter(std::string_view parentEmitterId, std::string_view displayName,
                                        std::string_view requestedId, std::string& outId, std::string& outMessage,
                                        VFX::VfxLayerKind kind) {
    if (!document_.IsOpen()) {
        outMessage = "No VFX document is open";
        return false;
    }
    const VFX::VfxEmitterData* parent = parentEmitterId.empty() ? nullptr : FindEmitter(parentEmitterId);
    if (!parentEmitterId.empty() && parent == nullptr) {
        outMessage = "Parent emitter was not found";
        return false;
    }
    // 流体域の下の流体源はその域へ、域の外の流体源（流体注入层）は届く場面の流体域へ注入する。
    const VFX::VfxEmitterData* domain = FindFluidDomainUnder(parentEmitterId);
    if (kind == VFX::VfxLayerKind::Fluid && domain != nullptr) {
        outMessage = "A fluid domain cannot be placed inside another fluid domain";
        return false;
    }
    const std::string id = requestedId.empty() ? AllocateStableId("emitter") : std::string(requestedId);
    if (ContainsStableId(id)) {
        outMessage = "VFX stable ID already exists: " + id;
        return false;
    }

    VFX::VfxEmitterData emitter = VFX::MakeDefaultVfxAssetData().emitters.front();
    emitter.kind = kind;
    RegenerateEmitterIds(emitter);
    emitter.id = id;
    VFX::PRESETS::ApplyLayerKindDefaults(emitter, kind);
    if (kind == VFX::VfxLayerKind::FluidSource && domain != nullptr) {
        // 新しい源は域の下寄りの球から上へ吹き、卷曲湍流で崩れる（そのまま翻卷して昇る烟になる値）。流体注入层は特效の
        // 位置で烟だけを入れる（力場を持たせると半径 0 の力場が場面の域全体を押すので付けない）。
        emitter.emitterTransform.transform.position = {0.0f, -0.325f * domain->fluidDomain.domainSize.y, 0.0f};
        emitter.fluidSource.velocity = {0.0f, 0.8f, 0.0f};
        emitter.forces.field.turbulence = 0.6f;
        emitter.forces.field.frequency = 2.0f;
        emitter.forces.field.evolution = 0.5f;
    }
    emitter.parentEmitterId = parentEmitterId;
    emitter.name = displayName.empty() ? "New Layer" : std::string(displayName);
    emitter.order =
        static_cast<uint32_t>(std::count_if(document_.Asset().emitters.begin(), document_.Asset().emitters.end(),
                                            [parentEmitterId](const VFX::VfxEmitterData& candidate) {
                                                return candidate.parentEmitterId == parentEmitterId;
                                            }));

    VFX::VfxAssetData next = document_.Asset();
    if (kind == VFX::VfxLayerKind::Adjustment) {
        emitter.order = 0;
        for (auto& sibling : next.emitters)
            if (sibling.parentEmitterId == parentEmitterId)
                ++sibling.order;
    }
    next.emitters.push_back(std::move(emitter));
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    outId = id;
    Select({VfxWorkspaceSelectionKind::Emitter, id, id});
    outMessage = "VFX emitter created";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::DuplicateEmitter(std::string_view emitterId, std::string& outId, std::string& outMessage) {
    const VFX::VfxEmitterData* source = FindEmitter(emitterId);
    if (source == nullptr) {
        outMessage = "VFX emitter was not found";
        return false;
    }
    std::vector<VFX::VfxEmitterData> copies{*source};
    for (const auto* child : VFX::CollectVfxLayerTree(document_.Asset(), source->id))
        copies.push_back(*child);
    std::unordered_map<std::string, std::string> ids;
    for (auto& copy : copies) {
        const auto previous = copy.id;
        RegenerateEmitterIds(copy);
        ids.emplace(previous, copy.id);
    }
    const auto id = copies.front().id;
    copies.front().name += " Copy";
    ++copies.front().order;
    auto next = document_.Asset();
    for (auto& sibling : next.emitters)
        if (sibling.parentEmitterId == source->parentEmitterId && sibling.order > source->order)
            ++sibling.order;
    for (auto& copy : copies) {
        if (const auto target = ids.find(copy.connectTarget.targetLayerId); target != ids.end())
            copy.connectTarget.targetLayerId = target->second;
        if (const auto matte = ids.find(copy.matte.sourceLayerId); matte != ids.end())
            copy.matte.sourceLayerId = matte->second;
        if (const auto parent = ids.find(copy.parentEmitterId); parent != ids.end())
            copy.parentEmitterId = parent->second;
        next.emitters.push_back(std::move(copy));
    }
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    outId = id;
    Select({VfxWorkspaceSelectionKind::Emitter, id, id});
    outMessage = "Layer subtree duplicated (" + std::to_string(copies.size()) + " layers)";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::RemoveEmitter(std::string_view emitterId, std::string& outMessage) {
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
    const std::string parentId = removed != nullptr ? removed->parentEmitterId : std::string{};
    for (VFX::VfxEmitterData& emitter : next.emitters) {
        if (emitter.matte.sourceLayerId == emitterId)
            emitter.matte.sourceLayerId.clear();
        if (emitter.connectTarget.targetLayerId == emitterId) {
            emitter.connectTarget.targetLayerId.clear();
            if (emitter.connectTarget.mode == VFX::VfxConnectTargetMode::LayerOrigin)
                emitter.connectTarget.mode = VFX::VfxConnectTargetMode::None;
        }
        if (emitter.parentEmitterId == emitterId) {
            emitter.parentEmitterId = parentId;
        }
    }
    std::erase_if(next.emitters, [emitterId](const VFX::VfxEmitterData& emitter) { return emitter.id == emitterId; });
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
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
    auto package = VFX::PRESETS::MakeVfxPresetAsset(presetId);
    auto next = document_.Asset();
    // 追加レイヤーの再生区間を含める。短い素材への追加でも検証を通す。
    next.durationSeconds = (std::max)(next.durationSeconds, package.durationSeconds);
    std::unordered_map<std::string, std::string> inputIds, emitterIds;
    for (auto& input : package.inputs) {
        const auto oldId = input.id;
        input.id = AllocateStableId("input");
        inputIds.emplace(oldId, input.id);
        next.inputs.push_back(std::move(input));
    }
    for (auto& emitter : package.emitters) {
        const auto oldId = emitter.id;
        RegenerateEmitterIds(emitter);
        emitterIds.emplace(oldId, emitter.id);
        for (auto& link : emitter.inputLinks) {
            if (const auto found = inputIds.find(link.inputId); found != inputIds.end())
                link.inputId = found->second;
        }
    }
    if (package.emitters.empty()) {
        outMessage = "Template has no effect layers";
        return false;
    }
    const auto selectedId = package.emitters.front().id;
    const bool style = preset->category == "Styles";
    if (style)
        for (auto& layer : next.emitters)
            if (layer.parentEmitterId.empty())
                ++layer.order;
    for (auto& emitter : package.emitters) {
        if (!emitter.parentEmitterId.empty())
            emitter.parentEmitterId = emitterIds.at(emitter.parentEmitterId);
        if (!emitter.matte.sourceLayerId.empty())
            emitter.matte.sourceLayerId = emitterIds.at(emitter.matte.sourceLayerId);
        if (const auto target = emitterIds.find(emitter.connectTarget.targetLayerId); target != emitterIds.end())
            emitter.connectTarget.targetLayerId = target->second;
        emitter.order = style ? 0u : static_cast<uint32_t>(next.emitters.size());
        next.emitters.push_back(std::move(emitter));
    }
    // One document transaction owns the complete composition and Undo.
    document_.SealMerge();
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    document_.SealMerge();
    Select({VfxWorkspaceSelectionKind::Emitter, selectedId, selectedId});
    outMessage = "Added template layers: " + preset->displayName;
    statusMessage_ = outMessage;
    return true;
}

} // namespace HIKARI::EDITOR
