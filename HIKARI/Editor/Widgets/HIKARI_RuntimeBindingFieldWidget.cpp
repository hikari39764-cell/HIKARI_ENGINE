#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Widgets/HIKARI_RuntimeBindingFieldWidget.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <vector>

#include "Core/Text/HIKARI_AsciiCase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {

        struct BindingChoice {
            std::string id{};
            std::string label{};
            std::string search{};
        };

        bool DrawChoices(
            std::string_view widgetId,
            std::span<const BindingChoice> choices,
            std::string& value) {
#if defined(HIKARI_WITH_EDITOR)
            const auto current = std::find_if(
                choices.begin(), choices.end(),
                [&value](const BindingChoice& choice) {
                    return choice.id == value;
                });
            const std::string preview = value.empty()
                ? std::string("<none>")
                : current != choices.end()
                    ? current->label
                    : "Missing: " + value;
            bool changed = false;
            const std::string id(widgetId);
            ImGui::PushID(id.c_str());
            if (ImGui::BeginCombo("##Binding", preview.c_str())) {
                static char searchBuffer[128]{};
                if (ImGui::IsWindowAppearing()) {
                    searchBuffer[0] = '\0';
                }
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText(TEXT::LocalizeWindow("editor.ui.search.bce064", "Search##RuntimeBinding").c_str(),
                    searchBuffer,
                    sizeof(searchBuffer));
                const std::string search =
                    TEXT::ToLowerAsciiCopy(searchBuffer);
                if (ImGui::Selectable(TEXT::LocalizeWindow("ui.common.none", "<none>").c_str(), value.empty())) {
                    value.clear();
                    changed = true;
                }
                for (const BindingChoice& choice : choices) {
                    if (!search.empty() &&
                        choice.search.find(search) == std::string::npos) {
                        continue;
                    }
                    const bool selected = choice.id == value;
                    if (ImGui::Selectable(
                            choice.label.c_str(), selected)) {
                        value = choice.id;
                        changed = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                if (choices.empty()) {
                    ImGui::TextDisabled(TEXT::Localize("editor.ui.no.compatible.bindings.151fec").c_str());
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
            return changed;
#else
            (void)widgetId;
            (void)choices;
            (void)value;
            return false;
#endif
        }

        void SortChoices(std::vector<BindingChoice>& choices) {
            std::sort(
                choices.begin(), choices.end(),
                [](const BindingChoice& lhs,
                   const BindingChoice& rhs) {
                    return lhs.search < rhs.search;
                });
        }

    } // namespace

    bool DrawRuntimeVariableField(
        const BINDING::Catalog& catalog,
        std::string_view widgetId,
        BINDING::ValueType expectedType,
        std::string& value) {

        const std::array allowedTypes{ expectedType };
        return DrawRuntimeVariableField(
            catalog, widgetId, allowedTypes, value);
    }

    bool DrawRuntimeVariableField(
        const BINDING::Catalog& catalog,
        std::string_view widgetId,
        std::span<const BINDING::ValueType> allowedTypes,
        std::string& value) {

        std::vector<BindingChoice> choices{};
        for (const BINDING::VariableDefinition& definition :
                catalog.variables) {
            if (std::find(
                    allowedTypes.begin(), allowedTypes.end(),
                    definition.defaultValue.type) == allowedTypes.end()) {
                continue;
            }
            BindingChoice choice{};
            choice.id = definition.id;
            choice.label = definition.name + "  [" +
                std::string(BINDING::ToString(
                    definition.defaultValue.type)) + "]##" +
                definition.id;
            choice.search = TEXT::ToLowerAsciiCopy(
                definition.name + " " + definition.id);
            choices.push_back(std::move(choice));
        }
        SortChoices(choices);
        return DrawChoices(widgetId, choices, value);
    }

    bool DrawRuntimeEventField(
        const BINDING::Catalog& catalog,
        std::string_view widgetId,
        bool hasPayload,
        BINDING::ValueType payloadType,
        std::string& value) {
        return DrawRuntimeEventField(
            catalog,
            widgetId,
            hasPayload
                ? AuthoringEventPayloadPolicy::Exact
                : AuthoringEventPayloadPolicy::None,
            payloadType,
            value);
    }

    bool DrawRuntimeEventField(
        const BINDING::Catalog& catalog,
        std::string_view widgetId,
        AuthoringEventPayloadPolicy policy,
        BINDING::ValueType payloadType,
        std::string& value) {

        std::vector<BindingChoice> choices{};
        for (const BINDING::EventDefinition& definition :
                catalog.events) {
            const bool compatible =
                policy == AuthoringEventPayloadPolicy::Any ||
                (policy == AuthoringEventPayloadPolicy::None &&
                 !definition.hasPayload) ||
                (policy == AuthoringEventPayloadPolicy::Exact &&
                 definition.hasPayload &&
                 definition.payloadType == payloadType);
            if (!compatible) {
                continue;
            }
            BindingChoice choice{};
            choice.id = definition.id;
            choice.label = definition.name;
            if (definition.hasPayload) {
                choice.label += "  [" +
                    std::string(BINDING::ToString(
                        definition.payloadType)) + "]";
            }
            choice.label += "##" + definition.id;
            choice.search = TEXT::ToLowerAsciiCopy(
                definition.name + " " + definition.id);
            choices.push_back(std::move(choice));
        }
        SortChoices(choices);
        return DrawChoices(widgetId, choices, value);
    }

} // namespace HIKARI::EDITOR
