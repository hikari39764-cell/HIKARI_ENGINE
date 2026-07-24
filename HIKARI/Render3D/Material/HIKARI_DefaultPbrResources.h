#pragma once

#include "Render3D/Core/HIKARI_Material.h"

namespace HIKARI {

    class DefaultPbrResources {
    public:
        static void Initialize();
        static void Shutdown();

        static RuntimeTextureSlot WhiteSlot();
        static RuntimeTextureSlot BlackSlot();
        static RuntimeTextureSlot FlatNormalSlot();
        static RuntimeTextureSlot MetallicRoughnessSlot();
        static RuntimeTextureSlot MissingSlot();
    };

} // namespace HIKARI
