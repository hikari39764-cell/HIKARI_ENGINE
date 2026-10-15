#include "Input/Assets/HIKARI_InputActionMap.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace HIKARI::INPUT {
namespace {

std::string BindingKey(const InputBinding& binding) {
    return std::to_string(static_cast<int>(binding.source)) + ":" + binding.control + ":" + binding.modifierControl;
}

} // namespace

const InputActionDefinition* InputActionMap::FindAction(std::string_view actionId) const {
    const auto it = std::find_if(actions.begin(), actions.end(), [actionId](const InputActionDefinition& action) {
        return action.actionId == actionId;
    });
    return it != actions.end() ? &*it : nullptr;
}

InputActionDefinition* InputActionMap::FindAction(std::string_view actionId) {
    return const_cast<InputActionDefinition*>(std::as_const(*this).FindAction(actionId));
}

const InputContextDefinition* InputActionMap::FindContext(std::string_view contextId) const {
    const auto it = std::find_if(contexts.begin(), contexts.end(), [contextId](const InputContextDefinition& context) {
        return context.contextId == contextId;
    });
    return it != contexts.end() ? &*it : nullptr;
}

InputContextDefinition* InputActionMap::FindContext(std::string_view contextId) {
    return const_cast<InputContextDefinition*>(std::as_const(*this).FindContext(contextId));
}

bool InputActionMap::RemoveAction(std::string_view actionId) {
    const size_t oldSize = actions.size();
    std::erase_if(actions, [actionId](const InputActionDefinition& action) { return action.actionId == actionId; });
    if (actions.size() == oldSize) {
        return false;
    }
    for (InputContextDefinition& context : contexts) {
        std::erase_if(context.bindings,
                      [actionId](const InputBinding& binding) { return binding.actionId == actionId; });
    }
    return true;
}

bool InputActionMap::RemoveContext(std::string_view contextId) {
    const size_t oldSize = contexts.size();
    std::erase_if(contexts,
                  [contextId](const InputContextDefinition& context) { return context.contextId == contextId; });
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
                issues.emplace_back("Context " + context.contextId + " references missing action " + binding.actionId +
                                    ".");
            }
            if (binding.control.empty()) {
                issues.emplace_back("Action " + binding.actionId + " has an empty control.");
            }
            if (binding.component > 1) {
                issues.emplace_back("Action " + binding.actionId + " uses an invalid component.");
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
            const auto [it, inserted] = ownerByBinding.emplace(key, binding.actionId);
            if (!inserted && it->second != binding.actionId) {
                conflicts.push_back(
                    InputBindingConflict{context.contextId, binding.control, it->second, binding.actionId});
            }
        }
    }
    return conflicts;
}

const char* ToString(InputActionValueType type) noexcept {
    return TEXT::EnumToName(kInputActionValueTypeNames, type, "Button");
}

const char* ToString(InputBindingSource source) noexcept {
    return TEXT::EnumToName(kInputBindingSourceNames, source, "Keyboard");
}

} // namespace HIKARI::INPUT
