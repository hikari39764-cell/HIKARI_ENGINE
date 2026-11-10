#include "Scene/HIKARI_WorldGround.h"

#include <cmath>

#include "HIKARI/Project/GroundSource.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

bool RaycastWorldGround(const World& world, const MATH::Vec3& origin, const MATH::Vec3& direction, float maxDistance,
                        WorldGroundHit& outHit) noexcept {
    const float length = MATH::Length(direction);
    if (!(length > 1.0e-6f) || !(maxDistance > 0.0f)) {
        return false;
    }
    const MATH::Vec3 unit = direction * (1.0f / length);
    bool found = false;
    for (const IGroundSource* source : world.GetGroundSources()) {
        GameProjectGroundHit hit{};
        try {
            if (!source->RaycastGround({origin.x, origin.y, origin.z}, {unit.x, unit.y, unit.z}, maxDistance, hit) ||
                !std::isfinite(hit.distance) || !std::isfinite(hit.position.y)) {
                continue;
            }
        } catch (...) {
            continue;
        }
        if (!found || hit.distance < outHit.distance) {
            outHit.position = {hit.position.x, hit.position.y, hit.position.z};
            outHit.normal = MATH::Normalize(MATH::Vec3{hit.normal.x, hit.normal.y, hit.normal.z});
            outHit.distance = hit.distance;
            found = true;
        }
    }
    return found;
}

uint64_t WorldGroundRevision(const World& world) noexcept {
    uint64_t revision = world.GetGroundSources().size();
    for (const IGroundSource* source : world.GetGroundSources()) {
        revision = revision * 1000003ull + source->GetGroundRevision();
    }
    return revision;
}

} // namespace HIKARI
