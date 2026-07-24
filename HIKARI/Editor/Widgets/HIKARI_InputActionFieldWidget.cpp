#include "Editor/Widgets/HIKARI_InputActionFieldWidget.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Input/Assets/HIKARI_InputActionMap.h"

#if defined(HIKARI_ENABLE_IMGUI)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    bool DrawInputActionField(
        const INPUT::InputActionMap& actionMap,
        std::string_view label,
        INPUT::InputActionValueType expectedType,
        std::string& inOutActionId) {
#if defined(HIKARI_ENABLE_IMGUI)
        std::vector<const INPUT::InputActionDefinition*> actions{};
        actions.reserve(actionMap.actions.size());
        for (const INPUT::InputActionDefinition& action :
                actionMap.actions) {
            if (action.valueType == expectedType) {
                actions.push_back(&action);
            }
        }
        std::sort(
            actions.begin(),
            actions.end(),
            [](const auto* lhs, const auto* rhs) {
                if (lhs->displayName != rhs->displayName) {
                    return lhs->displayName < rhs->displayName;
                }
                return lhs->actionId < rhs->actionId;
            });

        const INPUT::InputActionDefinition* selected =
            actionMap.FindAction(inOutActionId);
        std::string preview{};
        if (selected == nullptr) {
            preview = inOutActionId.empty()
                ? "<select action>"
                : "Missing: " + inOutActionId;
        } else if (selected->valueType != expectedType) {
            preview = "Wrong type: " + selected->displayName;
        } else {
            preview = selected->displayName.empty()
                ? selected->actionId
                : selected->displayName;
        }

        bool changed = false;
        const std::string labelText(label);
        if (ImGui::BeginCombo(labelText.c_str(), preview.c_str())) {
            for (const INPUT::InputActionDefinition* action : actions) {
                const bool isSelected =
                    action->actionId == inOutActionId;
                const std::string itemLabel =
                    (action->displayName.empty()
                        ? action->actionId
                        : action->displayName) +
                    "  [" + action->actionId + "]##" +
                    action->actionId;
                if (ImGui::Selectable(
                        itemLabel.c_str(),
                        isSelected)) {
                    inOutActionId = action->actionId;
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            if (actions.empty()) {
                ImGui::TextDisabled(
                    "No %s actions",
                    INPUT::ToString(expectedType));
            }
            ImGui::EndCombo();
        }
        return changed;
#else
        (void)actionMap;
        (void)label;
        (void)expectedType;
        (void)inOutActionId;
        return false;
#endif
    }

} // namespace HIKARI::EDITOR
