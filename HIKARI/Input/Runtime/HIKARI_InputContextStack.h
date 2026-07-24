#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace HIKARI::INPUT {

class InputActionMap;

class InputContextStack {
public:
    void ResetToDefaults(const InputActionMap& map);
    bool Push(std::string contextId);
    bool Pop(std::string_view contextId);
    void SetActive(std::string contextId, bool active);
    bool IsActive(std::string_view contextId) const;
    const std::vector<std::string>& GetActiveContexts() const noexcept {
        return activeContexts_;
    }

private:
    std::vector<std::string> activeContexts_{};
};

} // namespace HIKARI::INPUT
