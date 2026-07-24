#include "Vfx/Runtime/HIKARI_VfxAsset.h"

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI::VFX {

namespace {
const AssetRegistry* gAssetRegistry = nullptr;
}

void SetAssetRegistry(const AssetRegistry* registry) {
    gAssetRegistry = registry;
}

std::string ResolveEffectPath(const std::string& assetId) {
    if (assetId.empty()) {
        return {};
    }

    if (gAssetRegistry) {
        if (const auto* desc = gAssetRegistry->FindAs<VfxAssetDescriptor>(AssetId{ assetId })) {
            return desc->sourcePath;
        }
    }

    return assetId;
}

} // namespace HIKARI::VFX
