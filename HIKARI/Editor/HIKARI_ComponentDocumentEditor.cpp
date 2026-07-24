#include "HIKARI_ComponentDocumentEditor.h"

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"

#include <exception>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    bool ComponentDocumentEditor::DrawComponent(
        const ComponentRegistry& componentRegistry,
        SceneComponentData& componentData,
        IInspectorBuilder& inspectorBuilder,
        const InspectorContext& context) const {
        const ComponentTypeInfo* typeInfo = componentRegistry.Find(componentData.type);
        if (!typeInfo || !typeInfo->factory) {
            return false;
        }

        std::unique_ptr<IComponent> temporaryComponent = typeInfo->factory();
        if (!temporaryComponent) {
            return false;
        }

        nlohmann::json serialized = nlohmann::json::object();
        try {
            temporaryComponent->Deserialize(componentData.properties);
            inspectorBuilder.SetContext(context);
            temporaryComponent->BuildInspector(inspectorBuilder);
            temporaryComponent->Serialize(serialized);
        } catch (const std::exception& e) {
#if defined(HIKARI_WITH_EDITOR)
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                "Component inspector failed for %s: %s",
                componentData.type.c_str(),
                e.what());
#else
            (void)e;
#endif
            return false;
        } catch (...) {
#if defined(HIKARI_WITH_EDITOR)
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                "Component inspector failed for %s.",
                componentData.type.c_str());
#endif
            return false;
        }

        if (serialized == componentData.properties) {
            return false;
        }

        componentData.properties = std::move(serialized);
        return true;
    }

} // namespace HIKARI
