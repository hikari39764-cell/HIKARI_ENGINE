#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        std::string PropertyLabel(
            const VFX::VfxPropertySchema& schema) {

            return std::string(VFX::ToString(schema.moduleKind)) + "." +
                std::string(schema.propertyId);
        }
#endif
    } // namespace

    void VfxWorkspaceController::DrawEmitterBindingsInspector(
        const VFX::VfxEmitterData& emitter) {
#if defined(HIKARI_WITH_EDITOR)
        const VFX::VfxAssetData* asset = GetAsset();
        if (asset == nullptr) return;
        if (emitter.inputLinks.empty()) {
            ImGui::TextDisabled(
                VfxUiText("No bindings. Bind one exposed input to a runtime-mutable property.").c_str());
        }
        for (const VFX::VfxInputLinkData& link : emitter.inputLinks) {
            const VFX::VfxExposedInputData* input =
                FindInput(link.inputId);
            const VFX::VfxPropertySchema* schema =
                VFX::ResolveVfxPropertySchema(emitter, link.target);
            const std::string label =
                (input != nullptr ? input->name : link.inputId) +
                "  ->  " +
                (schema != nullptr
                    ? PropertyLabel(*schema)
                    : link.target.moduleId + "." +
                        link.target.propertyId);
            if (ImGui::Selectable(
                    label.c_str(),
                    selection_.kind == VfxWorkspaceSelectionKind::Link &&
                    selection_.targetId == link.id)) {
                Select({
                    VfxWorkspaceSelectionKind::Link,
                    link.id,
                    emitter.id,
                    link.target.moduleId,
                    link.target.propertyId,
                });
            }
        }

        if (ImGui::Button(VfxUiText("+ Bind Exposed Input").c_str())) {
            ImGui::OpenPopup("VfxAddInputBinding");
        }
        if (!ImGui::BeginPopup("VfxAddInputBinding")) return;
        if (asset->inputs.empty()) {
            ImGui::TextDisabled(VfxUiText("Create an exposed input first.").c_str());
            ImGui::EndPopup();
            return;
        }
        for (const VFX::VfxExposedInputData& input : asset->inputs) {
            if (!ImGui::BeginMenu(input.name.c_str())) continue;
            bool hasCompatibleTarget = false;
            for (const VFX::VfxPropertySchema& schema :
                 VFX::GetVfxPropertySchemas()) {
                if (!schema.bindable || !schema.runtimeMutable ||
                    !VFX::IsVfxInputTypeCompatible(input.type, schema)) {
                    continue;
                }
                const std::string_view moduleId =
                    GetVfxModuleId(emitter, schema.moduleKind);
                const auto duplicate = std::find_if(
                    emitter.inputLinks.begin(),
                    emitter.inputLinks.end(),
                    [&](const VFX::VfxInputLinkData& link) {
                        return link.target.moduleId == moduleId &&
                            link.target.propertyId == schema.propertyId;
                    });
                if (duplicate != emitter.inputLinks.end()) continue;
                hasCompatibleTarget = true;
                const std::string label = PropertyLabel(schema);
                if (ImGui::MenuItem(label.c_str())) {
                    std::string id{};
                    (void)AddInputLink(
                        emitter.id,
                        input.id,
                        moduleId,
                        schema.propertyId,
                        {},
                        id,
                        statusMessage_);
                }
            }
            if (!hasCompatibleTarget) {
                ImGui::TextDisabled(VfxUiText("No compatible unbound property").c_str());
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
#else
        (void)emitter;
#endif
    }

    void VfxWorkspaceController::DrawLinkInspector(
        AssetDatabase& database,
        const VFX::VfxEmitterData& source,
        const VFX::VfxInputLinkData& sourceLink) {
#if defined(HIKARI_WITH_EDITOR)
        const VFX::VfxEmitterData emitter = source;
        const VFX::VfxInputLinkData link = sourceLink;
        const VFX::VfxPropertySchema* currentSchema =
            VFX::ResolveVfxPropertySchema(emitter, link.target);
        const VFX::VfxExposedInputData* selectedInput =
            FindInput(link.inputId);
        ImGui::TextDisabled(VfxUiText("INPUT BINDING").c_str());
        ImGui::TextWrapped(TEXT::Localize("editor.ui.stable.id.s.f22a81").c_str(), link.id.c_str());

        if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.input.b568d4", "Input").c_str(),
                selectedInput != nullptr
                    ? selectedInput->name.c_str()
                    : link.inputId.c_str())) {
            for (const VFX::VfxExposedInputData& input :
                 document_.Asset().inputs) {
                if (currentSchema != nullptr &&
                    !VFX::IsVfxInputTypeCompatible(
                        input.type, *currentSchema)) {
                    continue;
                }
                if (ImGui::Selectable(
                        input.name.c_str(), input.id == link.inputId)) {
                    (void)SetProperty(
                        link.id,
                        "inputId",
                        input.id,
                        statusMessage_,
                        0u,
                        &database);
                }
            }
            ImGui::EndCombo();
        }

        const std::string currentTarget = currentSchema != nullptr
            ? PropertyLabel(*currentSchema)
            : link.target.moduleId + "." + link.target.propertyId;
        if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.property.9ae33a", "Property").c_str(), currentTarget.c_str())) {
            if (selectedInput != nullptr) {
                for (const VFX::VfxPropertySchema& schema :
                     VFX::GetVfxPropertySchemas()) {
                    if (!schema.bindable || !schema.runtimeMutable ||
                        !VFX::IsVfxInputTypeCompatible(
                            selectedInput->type, schema)) {
                        continue;
                    }
                    const std::string_view moduleId =
                        GetVfxModuleId(emitter, schema.moduleKind);
                    const std::string label = PropertyLabel(schema);
                    const bool selected =
                        link.target.moduleId == moduleId &&
                        link.target.propertyId == schema.propertyId;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        (void)SetProperty(
                            link.id,
                            "target",
                            {
                                { "emitterId", emitter.id },
                                { "moduleId", moduleId },
                                { "propertyId", schema.propertyId },
                            },
                            statusMessage_,
                            0u,
                            &database);
                    }
                }
            }
            ImGui::EndCombo();
        }
        VfxInspectorPropertyEditor editor(
            *this, database, link.id, statusMessage_);
        editor.Float("Scale", "scale", link.scale);
        editor.Float("Bias", "bias", link.bias);
        if (ImGui::Button(VfxUiText("Delete Binding").c_str())) {
            (void)RemoveInputLink(link.id, statusMessage_);
        }
#else
        (void)database;
        (void)source;
        (void)sourceLink;
#endif
    }

} // namespace HIKARI::EDITOR
