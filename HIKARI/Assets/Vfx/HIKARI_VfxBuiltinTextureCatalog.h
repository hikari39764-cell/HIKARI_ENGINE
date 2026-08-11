#pragma once

#include <span>
#include <string_view>

namespace HIKARI::VFX {

    struct VfxBuiltinTextureDescriptor {
        std::string_view assetId{};
        std::string_view displayName{};
    };

    std::span<const VfxBuiltinTextureDescriptor>
        GetVfxBuiltinTextureCatalog() noexcept;
    const VfxBuiltinTextureDescriptor* FindVfxBuiltinTexture(
        std::string_view assetId) noexcept;
    bool IsVfxBuiltinTextureAssetId(std::string_view assetId) noexcept;

} // namespace HIKARI::VFX
