#pragma once

#include <string>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {

struct SkyAsset {
    std::string name;
    std::string meshAssetName;
    std::string texturePath;
    SkyMode preferredMode = SkyMode::Gradient;
    bool textureIsLinear = false;
};

} // namespace HIKARI
