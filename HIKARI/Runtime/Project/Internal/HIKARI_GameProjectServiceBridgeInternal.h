#pragma once

#include <algorithm>
#include <iterator>
#include <string>
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"

namespace HIKARI::PROJECT::INTERNAL {

// 永続 View は世代ハンドルを解決してから現在 World と終了状態を検証する。
inline World* ResolveServiceWorld(GameProjectWorldHandle handle) noexcept {
    World* world = World::ResolveRuntimeWorld(handle.value);
    return CurrentWorld(world) != nullptr && !GameProjectSdkAccess::IsWorldClosing(*world) ? world : nullptr;
}

template <size_t Capacity>
bool ReadFixedUtf8(const char (&source)[Capacity], std::string& out) {
    static_assert(Capacity > 0u);
    const auto end = std::find(std::begin(source), std::end(source), '\0');
    if (end == std::end(source)) {
        out.clear();
        return false;
    }
    out.assign(source, end);
    return true;
}

} // namespace HIKARI::PROJECT::INTERNAL
