#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {
    class AssetDatabase;
}

namespace HIKARI::EDITOR {

    struct VfxParticleMaterialChoice {
        AssetGuid guid{};
        std::string label{};
    };

    struct VfxParticleMaterialCatalog {
        uint64_t contentRevision = 0xffffffffffffffffull;
        std::vector<VfxParticleMaterialChoice> choices{};
    };

    bool DrawVfxParticleMaterialField(
        AssetDatabase& assetDatabase,
        VfxParticleMaterialCatalog& catalog,
        const AssetGuid& current,
        AssetGuid& outSelected,
        AssetGuid& outCreatedAssetGuid,
        std::string& outMessage);

} // namespace HIKARI::EDITOR
