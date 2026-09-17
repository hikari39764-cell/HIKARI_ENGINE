#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"

#include <array>

namespace HIKARI::VFX {
    namespace {
        constexpr std::array<VfxBuiltinTextureDescriptor, 17u>
            kBuiltinTextures{ {
                { "engine.vfx.texture.slash_strip", "Slash Strip" },
                { "engine.vfx.texture.shockwave_strip", "Shockwave Strip" },
                { "engine.vfx.texture.flame_mask", "Flame Mask" },
                { "engine.vfx.texture.shield_cells", "Shield Cells" },
                { "engine.vfx.texture.flow_noise", "Flow Noise" },
                { "engine.vfx.texture.soft_disc", "Soft Disc" },
                { "engine.vfx.texture.lens_flare", "Lens Flare" },
                { "engine.vfx.texture.energy_ring", "Energy Ring" },
                { "engine.vfx.texture.magic_ring", "Magic Ring" },
                { "engine.vfx.texture.muzzle_flame", "Muzzle Flame" },
                { "engine.vfx.texture.lightning", "Lightning" },
                { "engine.vfx.texture.star_glint", "Star Glint" },
                { "engine.vfx.texture.energy_streak", "Energy Streak" },
                { "engine.vfx.texture.smoke_puff", "Smoke Puff" },
                { "engine.vfx.texture.flame_flipbook", "Flame Flipbook 16x4" },
                { "engine.vfx.texture.wispy_smoke_flipbook", "Wispy Smoke Flipbook 8x8" },
                { "engine.vfx.texture.explosion_flipbook", "Explosion Flipbook 5x5" },
            } };
    }

    std::span<const VfxBuiltinTextureDescriptor>
    GetVfxBuiltinTextureCatalog() noexcept {
        return kBuiltinTextures;
    }

    const VfxBuiltinTextureDescriptor* FindVfxBuiltinTexture(
        std::string_view assetId) noexcept {

        for (const VfxBuiltinTextureDescriptor& descriptor :
             kBuiltinTextures) {
            if (descriptor.assetId == assetId) {
                return &descriptor;
            }
        }
        return nullptr;
    }

    bool IsVfxBuiltinTextureAssetId(std::string_view assetId) noexcept {
        return FindVfxBuiltinTexture(assetId) != nullptr;
    }

} // namespace HIKARI::VFX
