#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"
#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxSerializedTarget.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"

namespace HIKARI::EDITOR {
namespace {

bool IsSameAddress(const VFX::VfxPropertyAddress& left, const VFX::VfxPropertyAddress& right) noexcept {
    return left.emitterId == right.emitterId && left.moduleId == right.moduleId && left.propertyId == right.propertyId;
}

bool ValidateParticleMaterial(AssetDatabase* database, const nlohmann::json& value, std::string& outMessage) {
    if (!value.is_string()) {
        outMessage = "Particle material GUID must be a string";
        return false;
    }
    const AssetGuid guid{value.get<std::string>()};
    if (!guid.IsValid())
        return true;
    if (database == nullptr) {
        outMessage = "Particle material assignment requires an open project";
        return false;
    }
    ASSETS::MATERIAL::MaterialAssetAuthoringService service(*database);
    ASSETS::MATERIAL::MaterialAuthoringDocument material{};
    if (!service.Load(guid, material, outMessage)) {
        outMessage = "Particle material could not be loaded: " + outMessage;
        return false;
    }
    if (!IsRenderableMaterialAssetKind(material.source.kind)) {
        outMessage = "KIND_MISMATCH: VFX renderers require a renderable "
                     "Material asset";
        return false;
    }
    return true;
}

bool ValidateParticleTexture(AssetDatabase* database, const nlohmann::json& value, std::string& outMessage) {
    if (!value.is_string()) {
        outMessage = "Particle texture GUID must be a string";
        return false;
    }
    const AssetGuid guid{value.get<std::string>()};
    if (!guid.IsValid() || VFX::IsVfxBuiltinTextureAssetId(guid.value)) {
        return true;
    }
    if (database == nullptr) {
        outMessage = "Particle texture assignment requires an open project";
        return false;
    }
    const AssetRecord* record = database->FindByGuid(guid);
    if (record == nullptr || record->type != AssetType::Texture) {
        outMessage = "VFX color texture must reference a Texture asset";
        return false;
    }
    return true;
}

float EmitterActiveDuration(const VFX::VfxAssetData& asset, const VFX::VfxEmitterData& emitter) noexcept {
    return emitter.activeDuration > 0.0f ? emitter.activeDuration
                                         : (std::max)(0.0f, asset.durationSeconds - emitter.startDelay);
}

void SortBurstTimeline(VFX::VfxEmitterData& emitter) {
    std::sort(emitter.spawn.burstTimeline.begin(), emitter.spawn.burstTimeline.end(),
              [](const VFX::VfxBurstKeyData& left, const VFX::VfxBurstKeyData& right) {
                  if (left.time != right.time) {
                      return left.time < right.time;
                  }
                  return left.id < right.id;
              });
}

} // namespace

bool VfxWorkspaceController::AddInput(VFX::VfxValueType type, std::string_view displayName,
                                      std::string_view requestedId, std::string& outId, std::string& outMessage) {
    if (!document_.IsOpen()) {
        outMessage = "No VFX document is open";
        return false;
    }
    const std::string id = requestedId.empty() ? AllocateStableId("input") : std::string(requestedId);
    if (ContainsStableId(id)) {
        outMessage = "VFX stable ID already exists: " + id;
        return false;
    }
    VFX::VfxExposedInputData input{};
    input.id = id;
    input.name = displayName.empty() ? "New " + std::string(VFX::ToString(type)) + " Input" : std::string(displayName);
    input.type = type;
    input.defaultValue = VFX::MakeDefaultVfxValue(type);
    VFX::VfxAssetData next = document_.Asset();
    next.inputs.push_back(std::move(input));
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    outId = id;
    Select({VfxWorkspaceSelectionKind::Input, id, {}});
    outMessage = "VFX exposed input created";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::RemoveInput(std::string_view inputId, std::string& outMessage) {
    if (FindInput(inputId) == nullptr) {
        outMessage = "VFX input was not found";
        return false;
    }
    VFX::VfxAssetData next = document_.Asset();
    std::erase_if(next.inputs, [inputId](const VFX::VfxExposedInputData& input) { return input.id == inputId; });
    size_t removedLinks = 0u;
    for (VFX::VfxEmitterData& emitter : next.emitters) {
        const size_t before = emitter.inputLinks.size();
        std::erase_if(emitter.inputLinks,
                      [inputId](const VFX::VfxInputLinkData& link) { return link.inputId == inputId; });
        removedLinks += before - emitter.inputLinks.size();
    }
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    preview_.previewInputOverrides_.erase(std::string(inputId));
    selection_ = {VfxWorkspaceSelectionKind::Inputs, "asset", {}};
    outMessage = "VFX input removed";
    if (removedLinks > 0u) {
        outMessage += "; removed " + std::to_string(removedLinks) + " dependent link(s)";
    }
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::AddInputLink(std::string_view emitterId, std::string_view inputId,
                                          std::string_view moduleId, std::string_view propertyId,
                                          std::string_view requestedId, std::string& outId, std::string& outMessage) {
    const VFX::VfxEmitterData* emitter = FindEmitter(emitterId);
    const VFX::VfxExposedInputData* input = FindInput(inputId);
    if (emitter == nullptr || input == nullptr) {
        outMessage = "VFX emitter or exposed input was not found";
        return false;
    }
    const VFX::VfxPropertyAddress address{
        std::string(emitterId),
        std::string(moduleId),
        std::string(propertyId),
    };
    const VFX::VfxPropertySchema* schema = VFX::ResolveVfxPropertySchema(*emitter, address);
    if (schema == nullptr || !schema->bindable || !schema->runtimeMutable) {
        outMessage = "VFX property address is not a runtime-bindable target";
        return false;
    }
    if (!VFX::IsVfxInputTypeCompatible(input->type, *schema)) {
        outMessage = "VFX input type is incompatible with property '" + std::string(VFX::ToString(schema->moduleKind)) +
                     "." + std::string(schema->propertyId) + "'";
        return false;
    }
    const auto duplicate =
        std::find_if(emitter->inputLinks.begin(), emitter->inputLinks.end(),
                     [&address](const VFX::VfxInputLinkData& link) { return IsSameAddress(link.target, address); });
    if (duplicate != emitter->inputLinks.end()) {
        outMessage = "VFX property already has an exposed-input binding";
        return false;
    }
    const std::string id = requestedId.empty() ? AllocateStableId("link") : std::string(requestedId);
    if (ContainsStableId(id)) {
        outMessage = "VFX stable ID already exists: " + id;
        return false;
    }
    VFX::VfxAssetData next = document_.Asset();
    VFX::VfxEmitterData* mutableEmitter = FindEmitterMutable(next, emitterId);
    mutableEmitter->inputLinks.push_back({id, std::string(inputId), address, 1.0f, 0.0f});
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    outId = id;
    Select(
        {VfxWorkspaceSelectionKind::Link, id, std::string(emitterId), std::string(moduleId), std::string(propertyId)});
    outMessage = "VFX input link created";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::RemoveInputLink(std::string_view linkId, std::string& outMessage) {
    const VFX::VfxEmitterData* owner = nullptr;
    if (FindInputLink(linkId, &owner) == nullptr || owner == nullptr) {
        outMessage = "VFX input link was not found";
        return false;
    }
    const std::string emitterId = owner->id;
    VFX::VfxAssetData next = document_.Asset();
    VFX::VfxEmitterData* emitter = FindEmitterMutable(next, emitterId);
    std::erase_if(emitter->inputLinks, [linkId](const VFX::VfxInputLinkData& link) { return link.id == linkId; });
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    Select({VfxWorkspaceSelectionKind::Bindings, emitterId, emitterId});
    outMessage = "VFX input link removed";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::SetProperty(std::string_view targetId, std::string_view propertyId,
                                         const nlohmann::json& value, std::string& outMessage, uint64_t mergeGroup,
                                         AssetDatabase* assetDatabase) {
    if (!document_.IsOpen()) {
        outMessage = "No VFX document is open";
        return false;
    }
    for (const VFX::VfxEmitterData& emitter : document_.Asset().emitters) {
        if (targetId == emitter.renderCommon.id && propertyId == "materialAssetGuid" &&
            !ValidateParticleMaterial(assetDatabase, value, outMessage)) {
            statusMessage_ = outMessage;
            return false;
        }
        if (targetId == emitter.renderCommon.id &&
            (propertyId == "colorTextureAssetGuid" || propertyId == "maskTextureAssetGuid" ||
             propertyId == "noiseTextureAssetGuid" || propertyId == "detailNoiseTextureAssetGuid" ||
             propertyId == "flowMapTextureAssetGuid") &&
            !ValidateParticleTexture(assetDatabase, value, outMessage)) {
            statusMessage_ = outMessage;
            return false;
        }
    }

    nlohmann::json serialized{};
    if (!BuildVfxSerializedDocument(document_.Asset(), serialized, outMessage)) {
        statusMessage_ = outMessage;
        return false;
    }
    if (!SetVfxSerializedProperty(
            serialized, targetId, propertyId, value,
            [this](std::string_view prefix) { return AllocateStableId(prefix); }, outMessage)) {
        statusMessage_ = outMessage;
        return false;
    }
    VFX::VfxAssetData next{};
    if (!ParseVfxSerializedDocument(serialized, next, outMessage)) {
        outMessage = "VFX property was rejected by the source contract: " + outMessage;
        statusMessage_ = outMessage;
        return false;
    }
    if (!ApplyAsset(std::move(next), outMessage, mergeGroup,
                    propertyId == "name" ? VfxDocumentImpact::Metadata : VfxDocumentImpact::Runtime)) {
        return false;
    }
    outMessage = "VFX property updated";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::AddBurstKey(std::string_view emitterId, float time, uint32_t count,
                                         std::string_view requestedId, std::string& outId, std::string& outMessage) {
    const VFX::VfxEmitterData* emitter = FindEmitter(emitterId);
    if (emitter == nullptr) {
        outMessage = "VFX emitter was not found";
        return false;
    }
    if (!std::isfinite(time) || time < 0.0f || time > EmitterActiveDuration(document_.Asset(), *emitter) ||
        count == 0u || count > emitter->spawn.maximumParticles) {
        outMessage = "Burst key time/count must fit the emitter timeline and "
                     "particle capacity";
        return false;
    }
    const std::string id = requestedId.empty() ? AllocateStableId("burst.key") : std::string(requestedId);
    if (ContainsStableId(id)) {
        outMessage = "VFX stable ID already exists: " + id;
        return false;
    }
    VFX::VfxAssetData next = document_.Asset();
    VFX::VfxEmitterData* mutableEmitter = FindEmitterMutable(next, emitterId);
    if (mutableEmitter == nullptr) {
        outMessage = "VFX emitter was not found";
        return false;
    }
    mutableEmitter->spawn.burstTimeline.push_back({id, time, count});
    SortBurstTimeline(*mutableEmitter);
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    outId = id;
    Select({VfxWorkspaceSelectionKind::BurstKey, id, std::string(emitterId)});
    outMessage = "VFX burst key created";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::SetBurstKey(std::string_view keyId, float time, uint32_t count, std::string& outMessage,
                                         uint64_t mergeGroup) {
    const VFX::VfxEmitterData* owner = nullptr;
    const VFX::VfxBurstKeyData* key = FindBurstKey(keyId, &owner);
    if (key == nullptr || owner == nullptr) {
        outMessage = "VFX burst key was not found";
        return false;
    }
    if (!std::isfinite(time) || time < 0.0f || time > EmitterActiveDuration(document_.Asset(), *owner) || count == 0u ||
        count > owner->spawn.maximumParticles) {
        outMessage = "Burst key time/count must fit the emitter timeline and "
                     "particle capacity";
        return false;
    }
    VFX::VfxAssetData next = document_.Asset();
    VFX::VfxEmitterData* mutableEmitter = FindEmitterMutable(next, owner->id);
    if (mutableEmitter == nullptr) {
        outMessage = "VFX emitter was not found";
        return false;
    }
    const auto found =
        std::find_if(mutableEmitter->spawn.burstTimeline.begin(), mutableEmitter->spawn.burstTimeline.end(),
                     [keyId](const VFX::VfxBurstKeyData& candidate) { return candidate.id == keyId; });
    if (found == mutableEmitter->spawn.burstTimeline.end()) {
        outMessage = "VFX burst key was not found";
        return false;
    }
    found->time = time;
    found->count = count;
    SortBurstTimeline(*mutableEmitter);
    if (!ApplyAsset(std::move(next), outMessage, mergeGroup)) {
        return false;
    }
    outMessage = "VFX burst key updated";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::RemoveBurstKey(std::string_view keyId, std::string& outMessage) {
    const VFX::VfxEmitterData* owner = nullptr;
    if (FindBurstKey(keyId, &owner) == nullptr || owner == nullptr) {
        outMessage = "VFX burst key was not found";
        return false;
    }
    const std::string ownerId = owner->id;
    VFX::VfxAssetData next = document_.Asset();
    VFX::VfxEmitterData* mutableEmitter = FindEmitterMutable(next, ownerId);
    if (mutableEmitter == nullptr) {
        outMessage = "VFX emitter was not found";
        return false;
    }
    std::erase_if(mutableEmitter->spawn.burstTimeline,
                  [keyId](const VFX::VfxBurstKeyData& candidate) { return candidate.id == keyId; });
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    Select({VfxWorkspaceSelectionKind::Emitter, ownerId, ownerId});
    outMessage = "VFX burst key removed";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::AddSpecialOutput(std::string_view emitterId, VFX::VfxSpecialOutputType type,
                                              std::string_view requestedId, std::string& outId,
                                              std::string& outMessage) {
    if (FindEmitter(emitterId) == nullptr) {
        outMessage = "VFX emitter was not found";
        return false;
    }
    if (type != VFX::VfxSpecialOutputType::WeaponPathDistortion && type != VFX::VfxSpecialOutputType::ScreenImpulse &&
        type != VFX::VfxSpecialOutputType::SkinnedAfterimage) {
        outMessage = "VFX special output type is invalid";
        return false;
    }
    const std::string id = requestedId.empty() ? AllocateStableId("special.output") : std::string(requestedId);
    if (ContainsStableId(id)) {
        outMessage = "VFX stable ID already exists: " + id;
        return false;
    }
    VFX::VfxAssetData next = document_.Asset();
    VFX::VfxEmitterData* mutableEmitter = FindEmitterMutable(next, emitterId);
    if (mutableEmitter == nullptr) {
        outMessage = "VFX emitter was not found";
        return false;
    }
    VFX::VfxSpecialOutputData output{};
    output.id = id;
    output.type = type;
    mutableEmitter->specialOutputs.push_back(std::move(output));
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    outId = id;
    Select({VfxWorkspaceSelectionKind::SpecialOutput, id, std::string(emitterId)});
    outMessage = "VFX special output created";
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::RemoveSpecialOutput(std::string_view outputId, std::string& outMessage) {
    const VFX::VfxEmitterData* owner = nullptr;
    if (FindSpecialOutput(outputId, &owner) == nullptr || owner == nullptr) {
        outMessage = "VFX special output was not found";
        return false;
    }
    const std::string ownerId = owner->id;
    VFX::VfxAssetData next = document_.Asset();
    VFX::VfxEmitterData* mutableEmitter = FindEmitterMutable(next, ownerId);
    if (mutableEmitter == nullptr) {
        outMessage = "VFX emitter was not found";
        return false;
    }
    std::erase_if(mutableEmitter->specialOutputs,
                  [outputId](const VFX::VfxSpecialOutputData& output) { return output.id == outputId; });
    if (!ApplyAsset(std::move(next), outMessage))
        return false;
    Select({VfxWorkspaceSelectionKind::Emitter, ownerId, ownerId});
    outMessage = "VFX special output removed";
    statusMessage_ = outMessage;
    return true;
}

} // namespace HIKARI::EDITOR
