#include "Core/Text/HIKARI_Localization.h"
#include "HIKARI_ComponentDocumentEditor.h"

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Editor/Authoring/HIKARI_ComponentAuthoringChanges.h"

#include <exception>

#include "imgui.h"

namespace HIKARI {

bool ComponentDocumentEditor::DrawComponent(const ComponentRegistry& componentRegistry,
                                            SceneComponentData& componentData, IInspectorBuilder& inspectorBuilder,
                                            const InspectorContext& context) const {
    const ComponentTypeInfo* typeInfo = componentRegistry.Find(componentData.type);
    if (!typeInfo || !typeInfo->factory) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.inspector.unavailable.for.component.type.s.463372").c_str(),
                            componentData.type.c_str());
        return false;
    }

    std::unique_ptr<IComponent> temporaryComponent = typeInfo->factory();
    if (!temporaryComponent) {
        return false;
    }

    nlohmann::json serialized = nlohmann::json::object();
    try {
        std::string deserializeDiagnostic{};
        if (!temporaryComponent->TryDeserialize(componentData.properties, deserializeDiagnostic)) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                               TEXT::Localize("editor.ui.component.data.is.invalid.for.s.s.c5f564").c_str(),
                               componentData.type.c_str(),
                               deserializeDiagnostic.empty() ? "deserialization failed"
                                                             : deserializeDiagnostic.c_str());
            return false;
        }
        nlohmann::json before;
        temporaryComponent->Serialize(before);
        inspectorBuilder.SetContext(context);
        temporaryComponent->BuildInspector(inspectorBuilder);
        temporaryComponent->Serialize(serialized);
        nlohmann::json candidate = componentData.properties;
        EDITOR::ApplyComponentAuthoringChanges(candidate, before, serialized);
        if (candidate == componentData.properties) {
            return false;
        }
        serialized = std::move(candidate);
        if (typeInfo->validateAuthoring) {
            const AuthoringCommandResult validation = typeInfo->validateAuthoring(*temporaryComponent, context);
            if (!validation.ok) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                                   TEXT::Localize("editor.ui.component.edit.rejected.s.9c95ed").c_str(),
                                   validation.message.empty() ? "authoring reference is invalid"
                                                              : validation.message.c_str());
                return false;
            }
        }
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           TEXT::Localize("editor.ui.component.inspector.failed.for.s.s.da6386").c_str(),
                           componentData.type.c_str(), e.what());
        return false;
    } catch (...) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           TEXT::Localize("editor.ui.component.inspector.failed.for.s.235dc2").c_str(),
                           componentData.type.c_str());
        return false;
    }

    componentData.properties = std::move(serialized);
    return true;
}

} // namespace HIKARI
