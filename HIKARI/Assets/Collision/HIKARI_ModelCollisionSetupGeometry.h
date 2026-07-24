#pragma once

#include <filesystem>
#include <string>

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"

namespace HIKARI::ASSETS::COLLISION {

    std::filesystem::path GetModelCollisionSetupGeometryPath(
        const std::filesystem::path& setupPath);

    bool LoadModelCollisionSetupGeometry(
        const std::filesystem::path& setupPath,
        ModelCollisionSetup& setup,
        std::string& outMessage);

    bool SaveModelCollisionSetupGeometry(
        const std::filesystem::path& setupPath,
        const ModelCollisionSetup& setup,
        std::string& outMessage);

} // namespace HIKARI::ASSETS::COLLISION
