#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"

#include <array>

namespace HIKARI::VFX {
    namespace {
        constexpr std::array<VfxBuiltinTextureDescriptor, 27u>
            kBuiltinTextures{ {
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
                { "engine.vfx.texture.comic_burst", "Comic Burst" },
                { "engine.vfx.texture.comic_slash_wedge", "Comic Slash Wedge" },
                { "engine.vfx.texture.comic_quad_shard", "Comic Quad Shard" },
                { "engine.vfx.texture.comic_broken_ring", "Comic Broken Ring" },
                { "engine.vfx.texture.comic_speed_lines", "Comic Speed Lines" },
                { "engine.vfx.texture.comic_ground_crack", "Comic Ground Crack" },
                { "engine.vfx.texture.comic_smoke_puff", "Comic Smoke Puff" },
                { "engine.vfx.texture.anime_v2_slash_crescent", "Anime V2 Slash Crescent" },
                { "engine.vfx.texture.anime_v2_spin_arc", "Anime V2 Spin Arc" },
                { "engine.vfx.texture.anime_v2_broken_ring", "Anime V2 Broken Ring" },
                { "engine.vfx.texture.anime_v2_radial_lines", "Anime V2 Radial Lines" },
                { "engine.vfx.texture.anime_v2_impact_crack", "Anime V2 Impact Crack" },
                { "engine.vfx.texture.anime_v2_energy_shards", "Anime V2 Energy Shards" },
                { "engine.vfx.texture.anime_v2_comic_smoke", "Anime V2 Comic Smoke" },
                { "engine.vfx.texture.anime_v2_vertical_cut", "Anime V2 Vertical Cut" },
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
