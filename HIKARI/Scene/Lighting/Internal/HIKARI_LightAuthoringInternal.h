#pragma once

#include <cstdint>

namespace HIKARI {
    struct SceneDocument;
}

namespace HIKARI::LIGHTING_AUTHORING::INTERNAL {

    void MigrateLegacyLights(
        SceneDocument& document,
        uint32_t sourceVersion);

} // namespace HIKARI::LIGHTING_AUTHORING::INTERNAL
