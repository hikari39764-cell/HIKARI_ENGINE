#include "Input/Assets/HIKARI_InputActionMap.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace HIKARI::INPUT {
namespace {

std::string BindingKey(const InputBinding& binding) {
    return std::to_string(static_cast<int>(binding.source)) + ":" +
        binding.control + ":" + binding.modifierControl;
}

} // namespace

const InputActionDefinition* InputActionMap::FindAction(
    std::string_view actionId) const {
    const auto it = std::find_if(actions.begin(), actions.end(),
        [actionId](const InputActionDefinition& action) {
            return action.actionId == actionId;
        });
    return it != actions.end() ? &*it : nullptr;
}

InputActionDefinition* InputActionMap::FindAction(std::string_view actionId) {
    return const_cast<InputActionDefinition*>(
        std::as_const(*this).FindAction(actionId));
}

const InputContextDefinition* InputActionMap::FindContext(
    std::string_view contextId) const {
    const auto it = std::find_if(contexts.begin(), contexts.end(),
        [contextId](const InputContextDefinition& context) {
            return context.contextId == contextId;
        });
    return it != contexts.end() ? &*it : nullptr;
}

InputContextDefinition* InputActionMap::FindContext(std::string_view contextId) {
    return const_cast<InputContextDefinition*>(
        std::as_const(*this).FindContext(contextId));
}

bool InputActionMap::RemoveAction(std::string_view actionId) {
    const size_t oldSize = actions.size();
    std::erase_if(actions, [actionId](const InputActionDefinition& action) {
        return action.actionId == actionId;
    });
    if (actions.size() == oldSize) {
        return false;
    }
    for (InputContextDefinition& context : contexts) {
        std::erase_if(context.bindings,
            [actionId](const InputBinding& binding) {
                return binding.actionId == actionId;
            });
    }
    return true;
}

bool InputActionMap::RemoveContext(std::string_view contextId) {
    const size_t oldSize = contexts.size();
    std::erase_if(contexts, [contextId](const InputContextDefinition& context) {
        return context.contextId == contextId;
    });
    return contexts.size() != oldSize;
}

std::vector<std::string> InputActionMap::Validate() const {
    std::vector<std::string> issues;
    std::unordered_set<std::string> actionIds;
    for (const InputActionDefinition& action : actions) {
        if (action.actionId.empty()) {
            issues.emplace_back("An input action has an empty ID.");
        } else if (!actionIds.insert(action.actionId).second) {
            issues.emplace_back("Duplicate input action: " + action.actionId);
        }
    }

    std::unordered_set<std::string> contextIds;
    for (const InputContextDefinition& context : contexts) {
        if (context.contextId.empty()) {
            issues.emplace_back("An input context has an empty ID.");
        } else if (!contextIds.insert(context.contextId).second) {
            issues.emplace_back("Duplicate input context: " + context.contextId);
        }
        for (const InputBinding& binding : context.bindings) {
            if (!actionIds.contains(binding.actionId)) {
                issues.emplace_back(
                    "Context " + context.contextId +
                    " references missing action " + binding.actionId + ".");
            }
            if (binding.control.empty()) {
                issues.emplace_back(
                    "Action " + binding.actionId + " has an empty control.");
            }
            if (binding.component > 1) {
                issues.emplace_back(
                    "Action " + binding.actionId + " uses an invalid component.");
            }
        }
    }
    return issues;
}

std::vector<InputBindingConflict> InputActionMap::FindConflicts() const {
    std::vector<InputBindingConflict> conflicts;
    for (const InputContextDefinition& context : contexts) {
        std::unordered_map<std::string, std::string> ownerByBinding;
        for (const InputBinding& binding : context.bindings) {
            const std::string key = BindingKey(binding);
            const auto [it, inserted] = ownerByBinding.emplace(
                key, binding.actionId);
            if (!inserted && it->second != binding.actionId) {
                conflicts.push_back(InputBindingConflict{
                    context.contextId,
                    binding.control,
                    it->second,
                    binding.actionId });
            }
        }
    }
    return conflicts;
}

const char* ToString(InputActionValueType type) noexcept {
    switch (type) {
    case InputActionValueType::Button: return "Button";
    case InputActionValueType::Axis1D: return "Axis1D";
    case InputActionValueType::Axis2D: return "Axis2D";
    default: return "Button";
    }
}

bool TryParseInputActionValueType(
    std::string_view value,
    InputActionValueType& out) noexcept {
    if (value == "Button") out = InputActionValueType::Button;
    else if (value == "Axis1D") out = InputActionValueType::Axis1D;
    else if (value == "Axis2D") out = InputActionValueType::Axis2D;
    else return false;
    return true;
}

const char* ToString(InputBindingSource source) noexcept {
    switch (source) {
    case InputBindingSource::Keyboard: return "Keyboard";
    case InputBindingSource::MouseButton: return "MouseButton";
    case InputBindingSource::MouseDeltaX: return "MouseDeltaX";
    case InputBindingSource::MouseDeltaY: return "MouseDeltaY";
    case InputBindingSource::MouseWheel: return "MouseWheel";
    case InputBindingSource::GamepadButton: return "GamepadButton";
    case InputBindingSource::GamepadAxis: return "GamepadAxis";
    default: return "Keyboard";
    }
}

bool TryParseInputBindingSource(
    std::string_view value,
    InputBindingSource& out) noexcept {
    if (value == "Keyboard") out = InputBindingSource::Keyboard;
    else if (value == "MouseButton") out = InputBindingSource::MouseButton;
    else if (value == "MouseDeltaX") out = InputBindingSource::MouseDeltaX;
    else if (value == "MouseDeltaY") out = InputBindingSource::MouseDeltaY;
    else if (value == "MouseWheel") out = InputBindingSource::MouseWheel;
    else if (value == "GamepadButton") out = InputBindingSource::GamepadButton;
    else if (value == "GamepadAxis") out = InputBindingSource::GamepadAxis;
    else return false;
    return true;
}

} // namespace HIKARI::INPUT
