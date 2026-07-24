#pragma once

#include <cstdint>
#include <string>

#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    struct WorldObjectDestroyedEvent {
        SceneObjectId objectId{};
        RuntimeObjectHandle runtimeObject{};
        std::string objectName{};
    };

} // namespace HIKARI
