#include "Scene/Water/HIKARI_WorldWater.h"

#include <cmath>

#include "HIKARI/Project/WaterSource.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::WATER {

bool SampleWorldWater(const World& world, const WaterSurfaceQuery& query, WaterSurfaceSample& outSample) noexcept {
    outSample = {};
    const auto* service = const_cast<World&>(world).Services().Find<WaterSurfaceQueryService>();
    if (service != nullptr) {
        (void)service->SampleNearest(query, outSample);
    }
    for (const IWaterSource* source : world.GetWaterSources()) {
        GameProjectWaterSourceSample water{};
        try {
            if (!source->SampleWater(query.positionWS.x, query.positionWS.z, water) ||
                !std::isfinite(water.surfaceHeight)) {
                continue;
            }
        } catch (...) {
            continue;
        }
        const float signedDistance = query.positionWS.y - water.surfaceHeight;
        if (outSample.valid && std::abs(outSample.signedDistanceToSurface) <= std::abs(signedDistance)) {
            continue;
        }
        WaterSurfaceSample sample{};
        sample.valid = true;
        sample.surfacePositionWS = {query.positionWS.x, water.surfaceHeight, query.positionWS.z};
        sample.velocityWS = {water.velocity.x, water.velocity.y, water.velocity.z};
        sample.signedDistanceToSurface = signedDistance;
        sample.submergenceDepth = signedDistance < 0.0f ? -signedDistance : 0.0f;
        sample.validFields = WaterSurfaceSampleField::Surface | WaterSurfaceSampleField::Velocity;
        if (water.hasBottom != 0u && std::isfinite(water.bottomHeight)) {
            sample.waterColumnDepth =
                water.surfaceHeight > water.bottomHeight ? water.surfaceHeight - water.bottomHeight : 0.0f;
            sample.validFields = sample.validFields | WaterSurfaceSampleField::ColumnDepth;
        }
        sample.evaluatedTimeSeconds = query.timeSeconds;
        outSample = sample;
    }
    return outSample.valid;
}

bool FindWorldOcean(const World& world, float& outLevel, RuntimeObjectHandle& outObject) noexcept {
    const auto* service = const_cast<World&>(world).Services().Find<WaterSurfaceQueryService>();
    return service != nullptr && service->FindOcean(outLevel, outObject);
}

} // namespace HIKARI::WATER
