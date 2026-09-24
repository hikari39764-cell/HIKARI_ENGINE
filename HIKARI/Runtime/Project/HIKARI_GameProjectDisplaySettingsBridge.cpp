#include "HIKARI/Project/DisplaySettingsView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_World.h"
#include "Runtime/Presentation/HIKARI_DisplaySettingsService.h"

namespace HIKARI {
namespace {
World* ResolveDisplayWorld(const WorldView& view) noexcept {
    World* world = GameProjectSdkAccess::GetWorld(view);
    return world != nullptr && RuntimeSceneContext::GetCurrentWorld() == world &&
                   !GameProjectSdkAccess::IsWorldClosing(*world)
               ? world
               : nullptr;
}

} // namespace
DisplaySettingsView WorldView::GetDisplaySettings() const noexcept { return DisplaySettingsView(*this); }
bool DisplaySettingsView::GetSettings(GameProjectDisplaySettings& out) const noexcept {
    out = {};
    try {
        auto* world = ResolveDisplayWorld(world_);
        return world && world->IsOwnerThread() && SERVICES::ReadDisplaySettings(out);
    } catch (...) {
        return false;
    }
}
bool DisplaySettingsView::GetCapabilities(GameProjectDisplayCapabilities& out) const noexcept {
    out = {};
    try {
        auto* world = ResolveDisplayWorld(world_);
        return world && world->IsOwnerThread() && SERVICES::ReadDisplayCapabilities(out);
    } catch (...) {
        return false;
    }
}
GameProjectDisplayApplyResult DisplaySettingsView::Apply(const GameProjectDisplaySettings& value) const noexcept {
    try {
        auto* world = ResolveDisplayWorld(world_);
        return world ? SERVICES::ApplyDisplaySettings(*world, value) : GameProjectDisplayApplyResult::InvalidWorld;
    } catch (...) {
        return GameProjectDisplayApplyResult::InvalidWorld;
    }
}
} // namespace HIKARI
