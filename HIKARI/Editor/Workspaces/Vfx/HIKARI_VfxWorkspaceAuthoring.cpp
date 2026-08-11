#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
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

        bool IsSameAddress(
            const VFX::VfxPropertyAddress& left,
            const VFX::VfxPropertyAddress& right) noexcept {

            return left.emitterId == right.emitterId &&
                left.moduleId == right.moduleId &&
                left.propertyId == right.propertyId;
        }

        bool ValidateParticleMaterial(
            AssetDatabase* database,
            const nlohmann::json& value,
            std::string& outMessage) {

            if (!value.is_string()) {
                outMessage = "Particle material GUID must be a string";
                return false;
            }
            const AssetGuid guid{ value.get<std::string>() };
            if (!guid.IsValid()) return true;
            if (database == nullptr) {
                outMessage =
                    "Particle material assignment requires an open project";
                return false;
            }
            ASSETS::MATERIAL::MaterialAssetAuthoringService service(
                *database);
            ASSETS::MATERIAL::MaterialAuthoringDocument material{};
            if (!service.Load(guid, material, outMessage)) {
                outMessage = "Particle material could not be loaded: " +
                    outMessage;
                return false;
            }
            if (!IsRenderableMaterialAssetKind(material.source.kind)) {
                outMessage =
                    "KIND_MISMATCH: VFX renderers require a renderable "
                    "Material asset";
                return false;
            }
            return true;
        }

        bool ValidateParticleTexture(
            AssetDatabase* database,
            const nlohmann::json& value,
            std::string& outMessage) {

            if (!value.is_string()) {
                outMessage = "Particle texture GUID must be a string";
                return false;
            }
            const AssetGuid guid{ value.get<std::string>() };
            if (!guid.IsValid() ||
                VFX::IsVfxBuiltinTextureAssetId(guid.value)) {
                return true;
            }
            if (database == nullptr) {
                outMessage =
                    "Particle texture assignment requires an open project";
                return false;
            }
            const AssetRecord* record = database->FindByGuid(guid);
            if (record == nullptr || record->type != AssetType::Texture) {
                outMessage =
                    "VFX color texture must reference a Texture asset";
                return false;
            }
            return true;
        }

    } // namespace

    bool VfxWorkspaceController::AddEmitter(
        std::string_view parentEmitterId,
        std::string_view displayName,
        std::string_view requestedId,
        std::string& outId,
        std::string& outMessage) {

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
        emitter.parentEmitterId = parentEmitterId;
        emitter.name = displayName.empty()
            ? "New Emitter"
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

    bool VfxWorkspaceController::AddInput(
        VFX::VfxValueType type,
        std::string_view displayName,
        std::string_view requestedId,
        std::string& outId,
        std::string& outMessage) {

        if (!document_.IsOpen()) {
            outMessage = "No VFX document is open";
            return false;
        }
        const std::string id = requestedId.empty()
            ? AllocateStableId("input")
            : std::string(requestedId);
        if (ContainsStableId(id)) {
            outMessage = "VFX stable ID already exists: " + id;
            return false;
        }
        VFX::VfxExposedInputData input{};
        input.id = id;
        input.name = displayName.empty()
            ? "New " + std::string(VFX::ToString(type)) + " Input"
            : std::string(displayName);
        input.type = type;
        input.defaultValue = VFX::MakeDefaultVfxValue(type);
        VFX::VfxAssetData next = document_.Asset();
        next.inputs.push_back(std::move(input));
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        outId = id;
        Select({ VfxWorkspaceSelectionKind::Input, id, {} });
        outMessage = "VFX exposed input created";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::RemoveInput(
        std::string_view inputId,
        std::string& outMessage) {

        if (FindInput(inputId) == nullptr) {
            outMessage = "VFX input was not found";
            return false;
        }
        VFX::VfxAssetData next = document_.Asset();
        std::erase_if(
            next.inputs,
            [inputId](const VFX::VfxExposedInputData& input) {
                return input.id == inputId;
            });
        size_t removedLinks = 0u;
        for (VFX::VfxEmitterData& emitter : next.emitters) {
            const size_t before = emitter.inputLinks.size();
            std::erase_if(
                emitter.inputLinks,
                [inputId](const VFX::VfxInputLinkData& link) {
                    return link.inputId == inputId;
                });
            removedLinks += before - emitter.inputLinks.size();
        }
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        previewInputOverrides_.erase(std::string(inputId));
        selection_ = { VfxWorkspaceSelectionKind::Inputs, "asset", {} };
        outMessage = "VFX input removed";
        if (removedLinks > 0u) {
            outMessage += "; removed " + std::to_string(removedLinks) +
                " dependent link(s)";
        }
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::AddInputLink(
        std::string_view emitterId,
        std::string_view inputId,
        std::string_view moduleId,
        std::string_view propertyId,
        std::string_view requestedId,
        std::string& outId,
        std::string& outMessage) {

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
        const VFX::VfxPropertySchema* schema =
            VFX::ResolveVfxPropertySchema(*emitter, address);
        if (schema == nullptr || !schema->bindable ||
            !schema->runtimeMutable) {
            outMessage =
                "VFX property address is not a runtime-bindable target";
            return false;
        }
        if (!VFX::IsVfxInputTypeCompatible(input->type, *schema)) {
            outMessage = "VFX input type is incompatible with property '" +
                std::string(VFX::ToString(schema->moduleKind)) + "." +
                std::string(schema->propertyId) + "'";
            return false;
        }
        const auto duplicate = std::find_if(
            emitter->inputLinks.begin(), emitter->inputLinks.end(),
            [&address](const VFX::VfxInputLinkData& link) {
                return IsSameAddress(link.target, address);
            });
        if (duplicate != emitter->inputLinks.end()) {
            outMessage =
                "VFX property already has an exposed-input binding";
            return false;
        }
        const std::string id = requestedId.empty()
            ? AllocateStableId("link")
            : std::string(requestedId);
        if (ContainsStableId(id)) {
            outMessage = "VFX stable ID already exists: " + id;
            return false;
        }
        VFX::VfxAssetData next = document_.Asset();
        VFX::VfxEmitterData* mutableEmitter =
            FindEmitterMutable(next, emitterId);
        mutableEmitter->inputLinks.push_back({
            id, std::string(inputId), address, 1.0f, 0.0f
        });
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        outId = id;
        Select({ VfxWorkspaceSelectionKind::Link, id,
            std::string(emitterId), std::string(moduleId),
            std::string(propertyId) });
        outMessage = "VFX input link created";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::RemoveInputLink(
        std::string_view linkId,
        std::string& outMessage) {

        const VFX::VfxEmitterData* owner = nullptr;
        if (FindInputLink(linkId, &owner) == nullptr || owner == nullptr) {
            outMessage = "VFX input link was not found";
            return false;
        }
        const std::string emitterId = owner->id;
        VFX::VfxAssetData next = document_.Asset();
        VFX::VfxEmitterData* emitter =
            FindEmitterMutable(next, emitterId);
        std::erase_if(
            emitter->inputLinks,
            [linkId](const VFX::VfxInputLinkData& link) {
                return link.id == linkId;
            });
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        Select({ VfxWorkspaceSelectionKind::Bindings,
            emitterId, emitterId });
        outMessage = "VFX input link removed";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::SetProperty(
        std::string_view targetId,
        std::string_view propertyId,
        const nlohmann::json& value,
        std::string& outMessage,
        uint64_t mergeGroup,
        AssetDatabase* assetDatabase) {

        if (!document_.IsOpen()) {
            outMessage = "No VFX document is open";
            return false;
        }
        for (const VFX::VfxEmitterData& emitter :
             document_.Asset().emitters) {
            if (targetId == emitter.renderCommon.id &&
                propertyId == "materialAssetGuid" &&
                !ValidateParticleMaterial(
                    assetDatabase, value, outMessage)) {
                statusMessage_ = outMessage;
                return false;
            }
            if (targetId == emitter.renderCommon.id &&
                propertyId == "colorTextureAssetGuid" &&
                !ValidateParticleTexture(
                    assetDatabase, value, outMessage)) {
                statusMessage_ = outMessage;
                return false;
            }
        }

        nlohmann::json serialized{};
        if (!BuildVfxSerializedDocument(
                document_.Asset(), serialized, outMessage)) {
            statusMessage_ = outMessage;
            return false;
        }
        if (!SetVfxSerializedProperty(
                serialized,
                targetId,
                propertyId,
                value,
                [this](std::string_view prefix) {
                    return AllocateStableId(prefix);
                },
                outMessage)) {
            statusMessage_ = outMessage;
            return false;
        }
        VFX::VfxAssetData next{};
        if (!ParseVfxSerializedDocument(serialized, next, outMessage)) {
            outMessage = "VFX property was rejected by the source contract: " +
                outMessage;
            statusMessage_ = outMessage;
            return false;
        }
        if (!ApplyAsset(std::move(next), outMessage, mergeGroup)) {
            return false;
        }
        outMessage = "VFX property updated";
        statusMessage_ = outMessage;
        return true;
    }

} // namespace HIKARI::EDITOR
