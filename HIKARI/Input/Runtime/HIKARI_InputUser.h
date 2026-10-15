#pragma once

#include <cstdint>
#include <optional>

namespace HIKARI::INPUT {

class InputUser {
public:
    explicit InputUser(uint32_t userId = 0) : userId_(userId) {}

    std::optional<uint32_t> GetGamepadIndex() const noexcept { return gamepadIndex_; }
    void AssignGamepad(uint32_t index) { gamepadIndex_ = index; }

private:
    uint32_t userId_ = 0;
    std::optional<uint32_t> gamepadIndex_{0u};
};

} // namespace HIKARI::INPUT
