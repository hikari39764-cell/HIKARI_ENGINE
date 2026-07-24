#pragma once

#include <cstdint>
#include <string>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    struct PrefabDocument {
        uint32_t version = 1;
        std::string prefabName{};
        SceneObjectData rootObject{};
    };

} // namespace HIKARI
