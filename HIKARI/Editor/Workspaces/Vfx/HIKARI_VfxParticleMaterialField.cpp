#include "Editor/Workspaces/Vfx/HIKARI_VfxParticleMaterialField.h"

#include <string>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
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
                    material.source.domain != MaterialDomain::Particle) {
                    continue;
                }
                choices.push_back({
                    record->guid,
                    GetAssetRecordDisplayName(*record),
                });
            }
            return choices;
        }

        bool IsParticleMaterial(
            AssetDatabase& database,
            const AssetGuid& guid,
            std::string& outMessage) {

            ASSETS::MATERIAL::MaterialAssetAuthoringService service(database);
            ASSETS::MATERIAL::MaterialAuthoringDocument material{};
            if (!service.Load(guid, material, outMessage)) {
                return false;
            }
            if (material.source.domain != MaterialDomain::Particle) {
                outMessage =
                    "DOMAIN_MISMATCH: VFX renderers accept only Particle Material assets";
                return false;
            }
            return true;
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
            ? "Built-in Particle Unlit"
            : selected != nullptr
                ? selected->label.c_str()
                : "Domain mismatch or missing material";
        if (ImGui::BeginCombo("Particle Material", preview)) {
            if (ImGui::Selectable(
                    "Built-in Particle Unlit", !current.IsValid())) {
                outSelected = {};
                changed = true;
            }
            for (const VfxParticleMaterialChoice& choice : catalog.choices) {
                if (ImGui::Selectable(
                        choice.label.c_str(), choice.guid == current)) {
                    outSelected = choice.guid;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", choice.guid.value.c_str());
                }
            }
            ImGui::EndCombo();
        }
        DroppedAssetPayload dropped{};
        if (AcceptAssetDropOfType(
                assetDatabase, AssetType::Material, dropped) &&
            dropped.record != nullptr) {
            std::string validationMessage{};
            if (IsParticleMaterial(
                    assetDatabase, dropped.guid, validationMessage)) {
                outSelected = dropped.guid;
                outMessage = "Particle Material assigned by drag and drop";
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
                "DOMAIN_MISMATCH: the assigned asset is not a Particle Material.");
        } else if (catalog.choices.empty()) {
            ImGui::TextDisabled(
                "No Particle Material exists yet. The built-in fallback remains usable.");
        }
        if (ImGui::Button("Create Particle Material")) {
            ASSETS::MATERIAL::MaterialAssetAuthoringService service(
                assetDatabase);
            const ASSETS::MATERIAL::MaterialAuthoringResult created =
                service.Create(
                    MaterialTemplateId::ParticleUnlit,
                    "New Particle Material");
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
        if (ImGui::Button("Edit Particle Material")) {
            outEditAssetGuid = current;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(
            "Procedural nodes, textures, Time, Position and Runtime Variables are supported.");
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

} // namespace HIKARI::EDITOR
