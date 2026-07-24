#pragma once

#include <string_view>
#include <unordered_map>
#include <vector>

#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringTypes.h"

namespace HIKARI::EDITOR {

    class SystemAuthoringRegistry {
    public:
        bool Register(SystemAuthoringDescriptor descriptor);
        void Clear() noexcept;
        const SystemAuthoringDescriptor* Find(
            std::string_view systemId) const noexcept;
        std::vector<std::string> GetSystemIds() const;

    private:
        std::unordered_map<std::string, SystemAuthoringDescriptor>
            bySystemId_{};
    };

} // namespace HIKARI::EDITOR
