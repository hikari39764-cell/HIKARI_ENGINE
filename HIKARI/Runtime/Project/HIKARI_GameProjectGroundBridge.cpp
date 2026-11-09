#include "HIKARI/Project/GroundSource.h"
#include "HIKARI/Project/WorldView.h"

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_WorldGround.h"

namespace HIKARI {

bool WorldView::AddGroundSource(IGroundSource& source) const noexcept {
    try {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr) {
            return false;
        }
        world->AddGroundSource(source);
        return true;
    } catch (...) {
        return false;
    }
}

void WorldView::RemoveGroundSource(IGroundSource& source) const noexcept {
    if (World* world = GameProjectSdkAccess::GetWorld(*this)) {
        world->RemoveGroundSource(source);
    }
}

bool WorldView::RaycastGround(GameProjectFloat3 origin, GameProjectFloat3 direction, float maxDistance,
                              GameProjectGroundHit& out) const noexcept {
    out = {};
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    WorldGroundHit hit{};
    if (world == nullptr || !RaycastWorldGround(*world, {origin.x, origin.y, origin.z},
                                                {direction.x, direction.y, direction.z}, maxDistance, hit)) {
        return false;
    }
    out.position = {hit.position.x, hit.position.y, hit.position.z};
    out.normal = {hit.normal.x, hit.normal.y, hit.normal.z};
    out.distance = hit.distance;
    return true;
}

} // namespace HIKARI
