#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"

#include <utility>

namespace HIKARI::EDITOR {

    bool SystemAuthoringRegistry::Register(
        SystemAuthoringDescriptor descriptor) {

        if (descriptor.systemId.empty()) {
            return false;
        }
        const std::string systemId = descriptor.systemId;
        return bySystemId_.emplace(
            systemId,
            std::move(descriptor)).second;
    }

    void SystemAuthoringRegistry::Clear() noexcept {
        bySystemId_.clear();
    }

    const SystemAuthoringDescriptor* SystemAuthoringRegistry::Find(
        std::string_view systemId) const noexcept {

        const auto found = bySystemId_.find(std::string(systemId));
        return found != bySystemId_.end() ? &found->second : nullptr;
    }

    std::vector<std::string>
        SystemAuthoringRegistry::GetSystemIds() const {

        std::vector<std::string> ids;
        ids.reserve(bySystemId_.size());
        for (const auto& [systemId, descriptor] : bySystemId_) {
            (void)descriptor;
            ids.push_back(systemId);
        }
        return ids;
    }

} // namespace HIKARI::EDITOR
