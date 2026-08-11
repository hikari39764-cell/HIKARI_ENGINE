#include "Editor/Workspaces/Vfx/HIKARI_VfxParticleMaterialField.h"

#include <string>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {

        std::vector<VfxParticleMaterialChoice> CollectParticleMaterials(
            AssetDatabase& database) {

            std::vector<VfxParticleMaterialChoice> choices{};
            ASSETS::MATERIAL::MaterialAssetAuthoringService service(database);
            for (const AssetRecord* record :
                 database.CollectByType(AssetType::Material)) {
                if (record == nullptr || !record->guid.IsValid()) continue;
                ASSETS::MATERIAL::MaterialAuthoringDocument material{};
                std::string message{};
                if (!service.Load(record->guid, material, message) ||
                    !IsRenderableMaterialAssetKind(material.source.kind)) {
                    continue;
                }
                choices.push_back({
                    record->guid,
                    GetAssetRecordDisplayName(*record) + " [" +
                        std::string(ToString(material.source.domain)) + "]",
                });
            }
            return choices;
        }

        bool IsVfxRenderableMaterial(
            AssetDatabase& database,
            const AssetGuid& guid,
            std::string& outMessage) {

            ASSETS::MATERIAL::MaterialAssetAuthoringService service(database);
            ASSETS::MATERIAL::MaterialAuthoringDocument material{};
            if (!service.Load(guid, material, outMessage)) {
                return false;
            }
            if (!IsRenderableMaterialAssetKind(material.source.kind)) {
                outMessage =
                    "KIND_MISMATCH: VFX renderers require a renderable Material asset";
                return false;
            }
            return true;
        }

        std::vector<VfxParticleTextureChoice> CollectParticleTextures(
            AssetDatabase& database) {

            std::vector<VfxParticleTextureChoice> choices{};
            for (const VFX::VfxBuiltinTextureDescriptor& descriptor :
                 VFX::GetVfxBuiltinTextureCatalog()) {
                choices.push_back({
                    AssetGuid{ std::string(descriptor.assetId) },
                    std::string(descriptor.displayName),
                    true,
                });
            }
            for (const AssetRecord* record :
                 database.CollectByType(AssetType::Texture)) {
                if (record == nullptr || !record->guid.IsValid()) continue;
                choices.push_back({
                    record->guid,
                    GetAssetRecordDisplayName(*record),
                    false,
                });
            }
            return choices;
        }

    } // namespace

    bool DrawVfxParticleMaterialField(
        AssetDatabase& assetDatabase,
        VfxParticleMaterialCatalog& catalog,
        const AssetGuid& current,
        AssetGuid& outSelected,
        AssetGuid& outCreatedAssetGuid,
        AssetGuid& outEditAssetGuid,
        std::string& outMessage) {
#if defined(HIKARI_WITH_EDITOR)
        outCreatedAssetGuid = {};
        outEditAssetGuid = {};
        const uint64_t contentRevision =
            assetDatabase.GetContentRevision();
        if (catalog.contentRevision != contentRevision) {
            catalog.choices = CollectParticleMaterials(assetDatabase);
            catalog.contentRevision = contentRevision;
        }
        const VfxParticleMaterialChoice* selected = nullptr;
        for (const VfxParticleMaterialChoice& choice : catalog.choices) {
            if (choice.guid == current) {
                selected = &choice;
                break;
            }
        }

        bool changed = false;
        const char* preview = !current.IsValid()
            ? "Built-in VFX Unlit"
            : selected != nullptr
                ? selected->label.c_str()
                : "Missing or non-renderable material";
        ImGui::BeginGroup();
        if (ImGui::BeginCombo("Material", preview)) {
            if (ImGui::Selectable(
                    "Built-in VFX Unlit", !current.IsValid())) {
                outSelected = {};
                changed = true;
            }
            for (const VfxParticleMaterialChoice& choice : catalog.choices) {
                ImGui::PushID(choice.guid.value.c_str());
                if (ImGui::Selectable(
                        choice.label.c_str(), choice.guid == current)) {
                    outSelected = choice.guid;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", choice.guid.value.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::EndGroup();
        DroppedAssetPayload dropped{};
        if (AcceptAssetDropOfType(
                assetDatabase, AssetType::Material, dropped) &&
            dropped.record != nullptr) {
            std::string validationMessage{};
            if (IsVfxRenderableMaterial(
                    assetDatabase, dropped.guid, validationMessage)) {
                outSelected = dropped.guid;
                outMessage = "VFX Material assigned by drag and drop";
                changed = true;
            } else {
                outMessage = validationMessage.empty()
                    ? "Dropped Material could not be loaded"
                    : std::move(validationMessage);
            }
        }

        if (current.IsValid() && selected == nullptr) {
            ImGui::TextColored(
                { 1.0f, 0.48f, 0.30f, 1.0f },
                "The assigned asset is missing or is not a renderable Material.");
        } else if (catalog.choices.empty()) {
            ImGui::TextDisabled(
                "No Material exists yet. The built-in VFX fallback remains usable.");
        }
        if (ImGui::Button("Create VFX Material")) {
            ASSETS::MATERIAL::MaterialAssetAuthoringService service(
                assetDatabase);
            const ASSETS::MATERIAL::MaterialAuthoringResult created =
                service.Create(
                    MaterialTemplateId::ParticleUnlit,
                    "New VFX Material");
            outMessage = created.message;
            if (created.success && created.assetGuid.IsValid()) {
                outSelected = created.assetGuid;
                outCreatedAssetGuid = created.assetGuid;
                catalog.contentRevision = 0xffffffffffffffffull;
                changed = true;
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!current.IsValid() || selected == nullptr);
        if (ImGui::Button("Edit Material")) {
            outEditAssetGuid = current;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(
            "Surface and Particle materials share the main runtime; particle geometry supplies UV and Tint.");
        return changed;
#else
        (void)assetDatabase;
        (void)catalog;
        (void)current;
        (void)outSelected;
        (void)outCreatedAssetGuid;
        (void)outEditAssetGuid;
        (void)outMessage;
        return false;
#endif
    }

    bool DrawVfxParticleTextureField(
        AssetDatabase& assetDatabase,
        VfxParticleTextureCatalog& catalog,
        const AssetGuid& current,
        AssetGuid& outSelected,
        std::string& outMessage) {
#if defined(HIKARI_WITH_EDITOR)
        const uint64_t contentRevision =
            assetDatabase.GetContentRevision();
        if (catalog.contentRevision != contentRevision) {
            catalog.choices = CollectParticleTextures(assetDatabase);
            catalog.contentRevision = contentRevision;
        }

        const VfxParticleTextureChoice* selected = nullptr;
        for (const VfxParticleTextureChoice& choice : catalog.choices) {
            if (choice.guid == current) {
                selected = &choice;
                break;
            }
        }
        const char* preview = !current.IsValid()
            ? "None (white particle)"
            : selected != nullptr
                ? selected->label.c_str()
                : "Missing texture";
        bool changed = false;
        ImGui::BeginGroup();
        if (ImGui::BeginCombo("Color Texture", preview)) {
            if (ImGui::Selectable(
                    "None (white particle)", !current.IsValid())) {
                outSelected = {};
                changed = true;
            }
            bool separatorDrawn = false;
            bool builtInHeaderDrawn = false;
            for (const VfxParticleTextureChoice& choice : catalog.choices) {
                if (!choice.builtIn && !separatorDrawn) {
                    ImGui::SeparatorText("Project Textures");
                    separatorDrawn = true;
                }
                if (choice.builtIn && !builtInHeaderDrawn) {
                    ImGui::SeparatorText("Engine VFX Textures");
                    builtInHeaderDrawn = true;
                }
                ImGui::PushID(choice.guid.value.c_str());
                if (ImGui::Selectable(
                        choice.label.c_str(), choice.guid == current)) {
                    outSelected = choice.guid;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", choice.guid.value.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::EndGroup();

        DroppedAssetPayload dropped{};
        if (AcceptAssetDropOfType(
                assetDatabase, AssetType::Texture, dropped) &&
            dropped.record != nullptr) {
            outSelected = dropped.guid;
            outMessage = "Particle color texture assigned by drag and drop";
            changed = true;
        }
        if (current.IsValid() && selected == nullptr) {
            ImGui::TextColored(
                { 1.0f, 0.48f, 0.30f, 1.0f },
                "The assigned particle texture is missing.");
        }
        ImGui::TextDisabled(
            "Engine VFX textures are shared; project Texture assets can be dropped here.");
        return changed;
#else
        (void)assetDatabase;
        (void)catalog;
        (void)current;
        (void)outSelected;
        (void)outMessage;
        return false;
#endif
    }

} // namespace HIKARI::EDITOR
