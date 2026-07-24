#pragma once

#include <string>

namespace HIKARI {
class AssetRegistry;
}

namespace HIKARI::VFX {

void SetAssetRegistry(const AssetRegistry* registry);
std::string ResolveEffectPath(const std::string& assetId);

} // namespace HIKARI::VFX
