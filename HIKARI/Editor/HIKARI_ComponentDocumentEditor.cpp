#include "HIKARI_ComponentDocumentEditor.h"

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Core/HIKARI_Logger.h"

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
            std::string deserializeDiagnostic{};
            if (!temporaryComponent->TryDeserialize(
                    componentData.properties,
                    deserializeDiagnostic)) {
#if defined(HIKARI_WITH_EDITOR)
                ImGui::TextColored(
                    ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                    "Component data is invalid for %s: %s",
                    componentData.type.c_str(),
                    deserializeDiagnostic.empty()
                        ? "deserialization failed"
                        : deserializeDiagnostic.c_str());
#endif
                return false;
            }
            inspectorBuilder.SetContext(context);
            temporaryComponent->BuildInspector(inspectorBuilder);
            temporaryComponent->Serialize(serialized);
            if (serialized == componentData.properties) {
                return false;
            }
            {
                std::string diff;
                for (auto it = serialized.begin();
                    it != serialized.end();
                    ++it) {
                    const bool same =
                        componentData.properties.contains(it.key()) &&
                        componentData.properties[it.key()] == it.value();
                    if (!same) {
                        diff += it.key() + "=" +
                            it.value().dump() + "!=" +
                            (componentData.properties.contains(it.key())
                                ? componentData.properties[it.key()].dump()
                                : "<missing>") + ";";
                    }
                }
                for (auto it = componentData.properties.begin();
                    it != componentData.properties.end();
                    ++it) {
                    if (!serialized.contains(it.key())) {
                        diff += "serialized-missing:" +
                            it.key() + ";";
                    }
                }
                if (diff.empty()) {
                    diff = "ser=" + serialized.dump() +
                        " props=" +
                        componentData.properties.dump();
                }
                HIKARI_LOG_INFO(
                    "[InspectorEdit] type=" +
                    componentData.type +
                    " diff=[" +
                    (diff.empty() ? std::string("<none>") : diff) +
                    "]");
            }
            if (typeInfo->validateAuthoring) {
                const AuthoringCommandResult validation =
                    typeInfo->validateAuthoring(
                        *temporaryComponent,
                        context);
                if (!validation.ok) {
#if defined(HIKARI_WITH_EDITOR)
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                        "Component edit rejected: %s",
                        validation.message.empty()
                            ? "authoring reference is invalid"
                            : validation.message.c_str());
#endif
                    return false;
                }
            }
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

        componentData.properties = std::move(serialized);
        return true;
    }

} // namespace HIKARI
