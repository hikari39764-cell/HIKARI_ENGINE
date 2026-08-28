#pragma once

#include "HIKARI_AssetDatabase.h"
#include "HIKARI_AssetRegistry.h"

namespace HIKARI {

    class AssetRegistryBuilder {
    public:
        bool RebuildRegistry(
            const AssetDatabase& assetDatabase,
            AssetRegistry& liveRegistry) const;
    };

} // namespace HIKARI
