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

    struct VfxParticleTextureChoice {
        AssetGuid guid{};
        std::string label{};
        bool builtIn = false;
    };

    struct VfxParticleTextureCatalog {
        uint64_t contentRevision = 0xffffffffffffffffull;
        std::vector<VfxParticleTextureChoice> choices{};
    };

    bool DrawVfxParticleMaterialField(
        AssetDatabase& assetDatabase,
        VfxParticleMaterialCatalog& catalog,
        const AssetGuid& current,
        AssetGuid& outSelected,
        AssetGuid& outCreatedAssetGuid,
        AssetGuid& outEditAssetGuid,
        std::string& outMessage);

    bool DrawVfxParticleTextureField(
        AssetDatabase& assetDatabase,
        VfxParticleTextureCatalog& catalog,
        const AssetGuid& current,
        AssetGuid& outSelected,
        std::string& outMessage);

} // namespace HIKARI::EDITOR
