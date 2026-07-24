#pragma once

#include <string>

#include "Assets/Collision/HIKARI_CollisionGeometryAsset.h"
#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"

namespace HIKARI::ASSETS::COLLISION {

    struct ModelCollisionCompileResult {
        bool success = false;
        uint32_t shapeCount = 0u;
        std::string message{};
    };

    ModelCollisionCompileResult CompileModelCollisionSetup(
        const ModelCollisionSetup& setup,
        CollisionGeometryAsset& outAsset);

} // namespace HIKARI::ASSETS::COLLISION
