#pragma once

#include <string>

#include "Assets/Collision/HIKARI_ModelCollisionMeshExtraction.h"
#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"

namespace HIKARI::ASSETS::COLLISION {

    bool FitCollisionPrimitive(
        const ModelCollisionMeshData& mesh,
        CollisionGeometryShapeType type,
        ModelCollisionShape& outShape,
        float& outRelativeError,
        std::string& outMessage);

} // namespace HIKARI::ASSETS::COLLISION
