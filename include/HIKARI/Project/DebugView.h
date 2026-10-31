#pragma once
#include <cstdint>
#include <string_view>
#include "HIKARI/Project/GameProjectSdk.h"

namespace HIKARI {
struct GameProjectSdkAccess;
struct GameProjectDebugReply {
    uint32_t success = 0;
    char text[8193]{};
};
// Callback-scoped, owner-thread diagnostics. The same commands back the editor
// console (~) and editor/AI operations. Disabled outside an explicit development session.
// Projects extend the command surface through declared runtime events (emit).
class DebugView final {
public:
    DebugView() = default;
    HIKARI_GAME_PROJECT_SDK_API bool Execute(std::string_view command, GameProjectDebugReply& reply) const noexcept;

private:
    friend struct GameProjectSdkAccess;
    explicit DebugView(void* world) noexcept : world_(world) {}
    void* world_ = nullptr;
};
} // namespace HIKARI
