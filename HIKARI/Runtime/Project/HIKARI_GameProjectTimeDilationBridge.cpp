#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectActionValidation.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Services/HIKARI_ActionTimeDilationService.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace HIKARI {
using namespace PROJECT::INTERNAL;

TimeDilationView GameProjectSdkAccess::MakeTimeDilationView(World& world) noexcept { return TimeDilationView(&world); }

TimeDilationView WorldView::GetTimeDilation() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeTimeDilationView(*world) : TimeDilationView{};
}

uint64_t TimeDilationView::Request(const GameProjectTimeDilationRequest& request) const noexcept {
    try {
        if (world_ == nullptr || request.sourceId == 0u || !std::isfinite(request.scale) ||
            !std::isfinite(request.durationUnscaledSeconds) || request.scale < 0.0f || request.scale > 1.0f ||
            request.durationUnscaledSeconds < 0.0f) {
            return 0u;
        }
        return SERVICES::GetActionTimeDilationService().Request(static_cast<World*>(world_), request.sourceId,
                                                                request.priority, request.scale,
                                                                request.durationUnscaledSeconds);
    } catch (...) {
        return 0u;
    }
}

bool TimeDilationView::Release(uint64_t token) const noexcept {
    try {
        return world_ != nullptr &&
               SERVICES::GetActionTimeDilationService().Release(static_cast<World*>(world_), token);
    } catch (...) {
        return false;
    }
}

float TimeDilationView::GetEffectiveScale() const noexcept {
    try {
        return world_ != nullptr
                   ? SERVICES::GetActionTimeDilationService().GetEffectiveScale(static_cast<World*>(world_))
                   : 1.0f;
    } catch (...) {
        return 1.0f;
    }
}
} // namespace HIKARI
