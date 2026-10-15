#pragma once

#include <cstdint>
#include <optional>

namespace HIKARI::INPUT {

class InputUser {
public:
    std::optional<uint32_t> GetGamepadIndex() const noexcept { return gamepadIndex_; }
    void AssignGamepad(uint32_t index) { gamepadIndex_ = index; }

private:
    std::optional<uint32_t> gamepadIndex_{0u};
};

} // namespace HIKARI::INPUT
