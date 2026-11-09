#include "HIKARI/Project/WaterSource.h"
#include "HIKARI/Project/WorldView.h"

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Water/HIKARI_WorldWater.h"

namespace HIKARI {

bool WorldView::AddWaterSource(IWaterSource& source) const noexcept {
    try {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr) {
            return false;
        }
        world->AddWaterSource(source);
        return true;
    } catch (...) {
        return false;
    }
}

void WorldView::RemoveWaterSource(IWaterSource& source) const noexcept {
    if (World* world = GameProjectSdkAccess::GetWorld(*this)) {
        world->RemoveWaterSource(source);
    }
}

bool WorldView::SampleWater(GameProjectFloat3 position, GameProjectWaterSample& out) const noexcept {
    out = {};
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    if (world == nullptr) {
        return false;
    }
    WATER::WaterSurfaceQuery query{};
    query.positionWS = {position.x, position.y, position.z};
    query.requestedFields = WATER::WaterSurfaceSampleField::Surface | WATER::WaterSurfaceSampleField::Velocity |
                            WATER::WaterSurfaceSampleField::ColumnDepth;
    WATER::WaterSurfaceSample sample{};
    if (!WATER::SampleWorldWater(*world, query, sample)) {
        return false;
    }
    out.valid = 1u;
    out.surfaceHeight = sample.surfacePositionWS.y;
    out.normal = {sample.normalWS.x, sample.normalWS.y, sample.normalWS.z};
    out.velocity = {sample.velocityWS.x, sample.velocityWS.y, sample.velocityWS.z};
    out.submergence = sample.submergenceDepth;
    out.columnDepth = WATER::HasSampleField(sample.validFields, WATER::WaterSurfaceSampleField::ColumnDepth)
                          ? sample.waterColumnDepth
                          : -1.0f;
    return true;
}

bool WorldView::FindOcean(GameProjectOcean& out) const noexcept {
    out = {};
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    RuntimeObjectHandle object{};
    if (world == nullptr || !WATER::FindWorldOcean(*world, out.level, object)) {
        return false;
    }
    const GameObject* owner = world->FindObject(object);
    out.sceneObjectId = owner != nullptr ? owner->GetDocumentId().value : 0u;
    return true;
}

} // namespace HIKARI
