#pragma once

#include "HIKARI_AssetDatabase.h"
#include "HIKARI_AssetRegistry.h"

namespace HIKARI {

    class AssetRegistryBuilder {
    public:
        bool AppendToRegistry(const AssetDatabase& assetDatabase, AssetRegistry& registry) const;
    };

} // namespace HIKARI
