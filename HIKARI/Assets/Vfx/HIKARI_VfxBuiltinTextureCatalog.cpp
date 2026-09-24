#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"

#include <array>

namespace HIKARI::VFX {
namespace {
constexpr std::array<VfxBuiltinTextureDescriptor, 53u> kBuiltinTextures{{
    {"engine.vfx.texture.slash_strip", "Slash Strip"},
    {"engine.vfx.texture.shockwave_strip", "Shockwave Strip"},
    {"engine.vfx.texture.beam_strip", "Beam Strip"},
    {"engine.vfx.texture.beam_needle", "Beam Needle"},
    {"engine.vfx.texture.flame_mask", "Flame Mask"},
    {"engine.vfx.texture.shield_cells", "Shield Cells"},
    {"engine.vfx.texture.flow_noise", "Flow Noise"},
    {"engine.vfx.texture.flow_stream", "Flow Map Stream (RG)"},
    {"engine.vfx.texture.flow_vortex", "Flow Map Vortex (RG)"},
    {"engine.vfx.texture.soft_disc", "Soft Disc"},
    {"engine.vfx.texture.lens_flare", "Lens Flare"},
    {"engine.vfx.texture.energy_ring", "Energy Ring"},
    {"engine.vfx.texture.magic_ring", "Magic Ring"},
    {"engine.vfx.texture.muzzle_flame", "Muzzle Flame"},
    {"engine.vfx.texture.lightning", "Lightning"},
    {"engine.vfx.texture.star_glint", "Star Glint"},
    {"engine.vfx.texture.energy_streak", "Energy Streak"},
    {"engine.vfx.texture.smoke_puff", "Smoke Puff"},
    {"engine.vfx.texture.flame_flipbook", "Flame Flipbook 16x4"},
    {"engine.vfx.texture.wispy_smoke_flipbook", "Wispy Smoke Flipbook 8x8"},
    {"engine.vfx.texture.explosion_flipbook", "Explosion Flipbook 5x5"},
    {"engine.vfx.texture.noise_fractal_1", "Noise Fractal 1"},
    {"engine.vfx.texture.noise_fractal_2", "Noise Fractal 2"},
    {"engine.vfx.texture.noise_fractal_3", "Noise Fractal 3"},
    {"engine.vfx.texture.noise_fractal_4", "Noise Fractal 4"},
    {"engine.vfx.texture.gradient_linear", "Gradient Linear"},
    {"engine.vfx.texture.gradient_soft", "Gradient Soft"},
    {"engine.vfx.texture.gradient_band", "Gradient Band"},
    {"engine.vfx.texture.gradient_falloff", "Gradient Falloff"},
    {"engine.vfx.texture.glow_soft", "Glow Soft"},
    {"engine.vfx.texture.glow_rgb", "Glow RGB"},
    {"engine.vfx.texture.glow_halo", "Glow Halo"},
    {"engine.vfx.texture.disc_thin", "Disc Thin"},
    {"engine.vfx.texture.disc_soft", "Disc Soft"},
    {"engine.vfx.texture.disc_large", "Disc Large"},
    {"engine.vfx.texture.particle_soft", "Particle Soft"},
    {"engine.vfx.texture.particle_hard", "Particle Hard"},
    {"engine.vfx.texture.star_soft", "Star Soft"},
    // CC0 (Effekseer tktk02) の細いライン。帯状の形状に載せると
    // 端がアルファで溶けるので、斬撃の弧や光条に向く。
    {"engine.vfx.texture.arc_line", "Arc Line"},
    // CC0 (Effekseer tktk02) の柔らかい光柱。断面が
    // 「明るい芯 + 長い減衰」なので、帯に載せると柔らかい縁になる。
    {"engine.vfx.texture.soft_line", "Soft Line"},
    // HIKARI 生成の高解像度ソフト線 (512x128)。断面は芯から
    // 長く減衰するので、長い弧でも柔らかさが保たれる。
    {"engine.vfx.texture.soft_line_hd", "Soft Line HD"},
    // HIKARI 生成の斬撃弧帯 (512x128)。断面は芯+長い減衰、輪郭は
    // ノイズで破れており、実行時の歪み無しで「割れた縁」が出る。
    {"engine.vfx.texture.slash_arc", "Slash Arc"},
    // HIKARI 生成の斬撃刃 (512x128)。U 方向へ尾から先端へ太くなり、
    // 破れと碎屑を含む。リング帯に載せると「刃」の形になる。
    {"engine.vfx.texture.slash_blade", "Slash Blade"},
    {"engine.vfx.texture.blur_wide", "Blur Wide"},
    {"engine.vfx.texture.blur_medium", "Blur Medium"},
    {"engine.vfx.texture.blur_tight", "Blur Tight"},
    {"engine.vfx.texture.streak_intense", "Streak Intense"},
    {"engine.vfx.texture.streak_line", "Streak Line"},
    {"engine.vfx.texture.distortion_noise", "Distortion Noise"},
    {"engine.vfx.texture.distortion_seamless", "Distortion Seamless"},
    {"engine.vfx.texture.shockwave_double", "Shockwave Double"},
    {"engine.vfx.texture.spark_dot", "Spark Dot"},
    {"engine.vfx.texture.lines_action", "Lines Action"},
}};
}

std::span<const VfxBuiltinTextureDescriptor> GetVfxBuiltinTextureCatalog() noexcept { return kBuiltinTextures; }

const VfxBuiltinTextureDescriptor* FindVfxBuiltinTexture(std::string_view assetId) noexcept {
    for (const VfxBuiltinTextureDescriptor& descriptor : kBuiltinTextures) {
        if (descriptor.assetId == assetId) {
            return &descriptor;
        }
    }
    return nullptr;
}

bool IsVfxBuiltinTextureAssetId(std::string_view assetId) noexcept { return FindVfxBuiltinTexture(assetId) != nullptr; }

} // namespace HIKARI::VFX
