#include "Input/Runtime/HIKARI_InputContextStack.h"

#include <algorithm>

#include "Input/Assets/HIKARI_InputActionMap.h"

namespace HIKARI::INPUT {

void InputContextStack::ResetToDefaults(const InputActionMap& map) {
    activeContexts_.clear();
    for (const InputContextDefinition& context : map.contexts) {
        if (context.enabledByDefault) {
            activeContexts_.push_back(context.contextId);
        }
    }
}

bool InputContextStack::Push(std::string contextId) {
    if (contextId.empty() || IsActive(contextId)) return false;
    activeContexts_.push_back(std::move(contextId));
    return true;
}

bool InputContextStack::Pop(std::string_view contextId) {
    const size_t oldSize = activeContexts_.size();
    std::erase(activeContexts_, contextId);
    return activeContexts_.size() != oldSize;
}

void InputContextStack::SetActive(std::string contextId, bool active) {
    if (active) (void)Push(std::move(contextId));
    else (void)Pop(contextId);
}

bool InputContextStack::IsActive(std::string_view contextId) const {
    return std::find(activeContexts_.begin(), activeContexts_.end(), contextId)
        != activeContexts_.end();
}

} // namespace HIKARI::INPUT
