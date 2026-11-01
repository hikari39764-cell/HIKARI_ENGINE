#include "RenderContracts/Material/HIKARI_MaterialEffectStack.h"
#include <algorithm>
#include <cmath>

namespace HIKARI {
bool SupportsSpatialMaterialEffect(MaterialEffectKind kind) noexcept {
    return kind == MaterialEffectKind::Polar || kind == MaterialEffectKind::Twirl || kind == MaterialEffectKind::Wave ||
           kind == MaterialEffectKind::TurbulentDisplace;
}
bool IsCompositeAdjustmentEffect(const MaterialEffectStep& effect) noexcept {
    return !SupportsSpatialMaterialEffect(effect.kind);
}
std::span<const std::string_view> GetMaterialEffectMasks(MaterialEffectSpace space) noexcept {
    static constexpr std::string_view masks[]{"None",     "Radial",      "Horizontal",
                                              "Vertical", "Input alpha", "Input luminance"};
    return std::span(masks).first(space == MaterialEffectSpace::LayerUv   ? 6
                                  : space == MaterialEffectSpace::Group3D ? 4
                                                                          : 0);
}
std::string_view ValidateMaterialEffect(const MaterialEffectStep& s) noexcept {
    const auto finite = [](const MATH::Vec4& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
    };
    if (GetMaterialEffectMasks(s.space).empty() ||
        (s.space == MaterialEffectSpace::Group3D && !SupportsSpatialMaterialEffect(s.kind)) ||
        size_t(s.mask) >= GetMaterialEffectMasks(s.space).size())
        return "Effect space requires a supported mapping and compatible mask";
    if (MaterialEffectName(s.kind) == "Invalid" || !finite(s.parameters) || !finite(s.parameters2) ||
        !finite(s.colorA) || !finite(s.colorB) || !finite(s.maskParameters) || !std::isfinite(s.mix) || s.mix < 0 ||
        s.mix > 1 || s.maskParameters.z < 0 || s.maskParameters.z > 2 || s.maskParameters.w < 0 ||
        s.maskParameters.w > 2)
        return "Effect values must be finite and blend/mask values within range";
    if (size_t(s.textureTarget) >= GetMaterialEffectTextureTargets(s.kind).size())
        return "Texture target is not supported by this effect";
    if (s.colorStops.size() > kMaximumMaterialEffectColorStops ||
        std::any_of(s.colorStops.begin(), s.colorStops.end(), [&](const auto& stop) {
            return !std::isfinite(stop.time) || stop.time < 0 || stop.time > 1 || !finite(stop.color);
        }))
        return "Gradient stops must be finite and within 0..1 and the stop limit";
    return {};
}
namespace {
constexpr std::string_view names[]{
#define HIKARI_EFFECT(name, id) #name,
#include "Include/Vfx/HIKARI_MaterialEffectKinds.hlsli"
#undef HIKARI_EFFECT
};
} // namespace
std::span<const std::string_view> GetMaterialEffectTextureTargets() noexcept {
    static constexpr std::array<std::string_view, 6> targets{"Shared", "BaseColor",   "Mask",
                                                             "Noise",  "DetailNoise", "FlowMap"};
    return targets;
}
std::span<const std::string_view> GetMaterialEffectTextureTargets(MaterialEffectKind kind) noexcept {
    const auto targets = GetMaterialEffectTextureTargets();
    return targets.first(kind == MaterialEffectKind::UvFlow    ? targets.size()
                         : kind == MaterialEffectKind::FlowMap ? 3
                                                               : 1);
}
std::string_view MaterialEffectTextureTargetName(MaterialEffectTextureTarget target) noexcept {
    const auto targets = GetMaterialEffectTextureTargets();
    return static_cast<size_t>(target) < targets.size() ? targets[static_cast<size_t>(target)] : "Invalid";
}
bool ParseMaterialEffectTextureTarget(std::string_view name, MaterialEffectTextureTarget& target) noexcept {
    const auto targets = GetMaterialEffectTextureTargets();
    for (size_t i = 0; i < targets.size(); ++i) {
        if (targets[i] == name) {
            target = static_cast<MaterialEffectTextureTarget>(i);
            return true;
        }
    }
    return false;
}
MaterialEffectStage GetMaterialEffectStage(MaterialEffectKind kind) noexcept {
    switch (kind) {
    case MaterialEffectKind::Pixelate:
    case MaterialEffectKind::Polar:
    case MaterialEffectKind::Twirl:
    case MaterialEffectKind::Wave:
    case MaterialEffectKind::FlowMap:
    case MaterialEffectKind::UvFlow:
    case MaterialEffectKind::UvDistortion:
    case MaterialEffectKind::TurbulentDisplace: return MaterialEffectStage::Uv;
    case MaterialEffectKind::Blur: return MaterialEffectStage::Blur;
    case MaterialEffectKind::Streak: return MaterialEffectStage::Streak;
    case MaterialEffectKind::RadialBlur: return MaterialEffectStage::RadialBlur;
    default: return MaterialEffectStage::Surface;
    }
}
std::string_view MaterialEffectStageName(MaterialEffectStage stage) noexcept {
    switch (stage) {
    case MaterialEffectStage::Uv: return "UV mapping";
    case MaterialEffectStage::Blur: return "Texture blur";
    case MaterialEffectStage::Streak: return "Texture streak";
    case MaterialEffectStage::RadialBlur: return "Texture radial blur";
    case MaterialEffectStage::Surface: return "Color, coverage & emission";
    default: return "Invalid";
    }
}
bool IsAdjustmentMaterialEffect(MaterialEffectKind kind) noexcept {
    // 時間依存の粒子専用効果は調整の時計へ暗黙に読み替えない。
    switch (kind) {
    case MaterialEffectKind::UvFlow:
    case MaterialEffectKind::FlowMap:
    case MaterialEffectKind::UvDistortion:
    case MaterialEffectKind::Fade:
    case MaterialEffectKind::Wipe:
    case MaterialEffectKind::Dissolve:
    case MaterialEffectKind::Emission:
    case MaterialEffectKind::Fresnel: return false;
    default: return MaterialEffectName(kind) != "Invalid";
    }
}
MaterialEffectCategory GetMaterialEffectCategory(MaterialEffectKind kind) noexcept {
    using C = MaterialEffectCategory;
    switch (kind) {
    case MaterialEffectKind::UvFlow:
    case MaterialEffectKind::FlowMap:
    case MaterialEffectKind::UvDistortion:
    case MaterialEffectKind::TurbulentDisplace:
    case MaterialEffectKind::Polar:
    case MaterialEffectKind::Twirl:
    case MaterialEffectKind::Wave: return C::Mapping;
    case MaterialEffectKind::Fade:
    case MaterialEffectKind::Wipe:
    case MaterialEffectKind::Dissolve:
    case MaterialEffectKind::Opacity:
    case MaterialEffectKind::LuminanceMask:
    case MaterialEffectKind::AlphaMorph:
    case MaterialEffectKind::Noise: return C::Mask;
    case MaterialEffectKind::Gradient:
    case MaterialEffectKind::ColorCorrect:
    case MaterialEffectKind::Levels:
    case MaterialEffectKind::ChannelMixer:
    case MaterialEffectKind::Duotone: return C::Color;
    case MaterialEffectKind::Emission:
    case MaterialEffectKind::Fresnel:
    case MaterialEffectKind::Glow:
    case MaterialEffectKind::LightSweep: return C::Light;
    case MaterialEffectKind::Blur:
    case MaterialEffectKind::RadialBlur:
    case MaterialEffectKind::Streak:
    case MaterialEffectKind::Chromatic:
    case MaterialEffectKind::Outline: return C::Filter;
    default: return C::Style;
    }
}
std::string_view MaterialEffectCategoryName(MaterialEffectCategory category) noexcept {
    constexpr std::string_view categories[]{"Mapping", "Mask", "Color", "Light", "Filter", "Style"};
    const auto index = static_cast<size_t>(category);
    return index < std::size(categories) ? categories[index] : "Invalid";
}
bool MaterialEffectUsesColor(MaterialEffectKind kind, bool secondary) noexcept {
    if (secondary)
        return kind == MaterialEffectKind::Gradient || kind == MaterialEffectKind::Duotone ||
               kind == MaterialEffectKind::Threshold || kind == MaterialEffectKind::Halftone;
    return MaterialEffectUsesColor(kind, true) || kind == MaterialEffectKind::Dissolve ||
           kind == MaterialEffectKind::Emission || kind == MaterialEffectKind::Fresnel ||
           kind == MaterialEffectKind::Glow || kind == MaterialEffectKind::LightSweep ||
           kind == MaterialEffectKind::Ink || kind == MaterialEffectKind::Outline;
}
static std::array<MaterialEffectParameterInfo, 4> DescribeParameters(MaterialEffectKind kind, bool secondary) noexcept {
    using Info = MaterialEffectParameterInfo;
    const auto P = [](const char* name, float lo = 0, float hi = 1, bool animated = true) {
        return Info{name, lo, hi, animated};
    };
    if (secondary) {
        switch (kind) {
        case MaterialEffectKind::UvFlow:
            return {P("rotation", -360, 360), P("swapAxes", 0, 1, false), P("pivotU", -2, 2, false),
                    P("pivotV", -2, 2, false)};
        case MaterialEffectKind::Glow: return {P("farRadius", 0, 1, false), P("farWeight", 0, 4, false)};
        case MaterialEffectKind::RadialBlur: return {P("direction", -1, 1)};
        case MaterialEffectKind::ColorCorrect: return {P("hue", -180, 180)};
        case MaterialEffectKind::Polar: return {P("inverse", 0, 1, false)};
        case MaterialEffectKind::Wave:
            return {P("radial", 0, 1, false), P("centerU", -2, 2), P("centerV", -2, 2), P("angle", -360, 360)};
        case MaterialEffectKind::Noise:
            return {P("cellular", 0, 1, false), P("seed", 0, 1000), P("maskMix"), P("colorMix")};
        case MaterialEffectKind::Ink:
            return {P("bleed", 0, 0.05f), P("paperScale", 0.1f, 8), P("dryBrush"), P("paperStrength")};
        case MaterialEffectKind::FlowMap:
            return {P("direction", -180, 180), P("speedVariation"), P("channelBModulation", 0, 1, false)};
        default: return {};
        }
    }
    const float radius = 0.5f;
    switch (kind) {
    case MaterialEffectKind::UvFlow:
        return {P("speedU", -20, 20), P("speedV", -20, 20), P("repeatU", 0.001f, 20), P("repeatV", 0.001f, 20)};
    case MaterialEffectKind::FlowMap:
        return {P("strength", 0, 2), P("speed", -8, 8), P("repeat", 0.001f, 20), P("mix")};
    case MaterialEffectKind::UvDistortion:
        return {P("strength"), P("noiseScale", 0.01f, 64, false), P("speed", -8, 8, false)};
    case MaterialEffectKind::Fade: return {P("fadeIn", 0, 1, false), P("fadeOut", 0, 1, false)};
    case MaterialEffectKind::Wipe:
        return {P("start", 0, 1, false), P("end", 0, 1, false), P("softness", 0.001f, 1, false),
                P("axis", 0, 1, false)};
    case MaterialEffectKind::Dissolve:
        return {P("start", 0, 1, false), P("end", 0, 1, false), P("noiseScale", 0.01f, 64, false),
                P("edgeWidth", 0.001f, 1, false)};
    case MaterialEffectKind::Emission:
        return {P("intensity", 0, 32), P("pulse", 0, 1, false), P("frequency", 0, 32, false)};
    case MaterialEffectKind::Fresnel:
        return {P("power", 0.01f, 16, false), P("intensity", 0, 32), P("rimOpacity", 0, 1, false)};
    case MaterialEffectKind::Opacity: return {P("amount")};
    case MaterialEffectKind::Glow:
        return {P("threshold", 0, 16), P("intensity", 0, 32), P("softness", 0.001f, 1), P("radius", 0, 0.5f)};
    case MaterialEffectKind::Blur:
        return {P("radius", 0, radius), P("angle", -180, 180, false), P("taps", 1, 6, false),
                P("crossBlur", 0, 1, false)};
    case MaterialEffectKind::LightSweep:
        return {P("center", -1, 2), P("width", 0.001f, 2), P("angle", -180, 180), P("intensity", 0, 32)};
    case MaterialEffectKind::LuminanceMask:
        return {P("threshold"), P("softness", 0.001f, 1), P("replace", 0, 1, false)};
    case MaterialEffectKind::RadialBlur:
        return {P("radius", 0, 0.5f), P("centerU", -1, 2), P("centerV", -1, 2), P("taps", 1, 4.0f, false)};
    case MaterialEffectKind::TurbulentDisplace:
        return {P("amount", 0, 0.5f), P("frequency", 0.01f, 32), P("speed", -8, 8), P("detail", 0, 1)};
    case MaterialEffectKind::ColorCorrect:
        return {P("exposure", 0, 8), P("contrast", 0, 4), P("saturation", 0, 4), P("blackPoint", -1, 1)};
    case MaterialEffectKind::Streak:
        return {P("length", 0, radius), P("angle", -180, 180), P("taps", 1, 8.0f, false), P("decay")};
    case MaterialEffectKind::Pixelate: return {P("cellSize", 0.001f, 1), P("mix")};
    case MaterialEffectKind::Posterize: return {P("levels", 2, 64), P("mix")};
    case MaterialEffectKind::Ink: return {P("edgeWidth", 0.001f, 0.05f), P("intensity", 0, 4), P("detail"), P("mix")};
    case MaterialEffectKind::Noise:
        return {P("frequency", 0.01f, 64), P("evolution", -100, 100), P("detail"), P("contrast", 0.01f, 8)};
    case MaterialEffectKind::Polar: return {P("mix"), P("centerU", -2, 2), P("centerV", -2, 2), P("scale", 0.001f, 8)};
    case MaterialEffectKind::Twirl:
        return {P("angle", -720, 720), P("radius", 0.001f, 2), P("centerU", -2, 2), P("centerV", -2, 2)};
    case MaterialEffectKind::Wave:
        return {P("amount", 0, 0.5f), P("frequency", 0.01f, 64), P("phase", -100, 100), P("falloff", 0, 8)};
    case MaterialEffectKind::AlphaMorph: return {P("radius", -0.125f, 0.125f), P("feather", 0, 0.0625f), P("mix")};
    case MaterialEffectKind::Outline: return {P("width", 0, 0.125f), P("inner", 0, 1, false), P("opacity"), P("mix")};
    case MaterialEffectKind::Levels:
        return {P("blackPoint", 0, 4), P("whitePoint", 0.001f, 16), P("gamma", 0.05f, 8), P("mix")};
    case MaterialEffectKind::ChannelMixer:
        return {P("redWeight", -2, 2), P("greenWeight", -2, 2), P("blueWeight", -2, 2), P("maskMix")};
    case MaterialEffectKind::Chromatic:
        return {P("distance", -0.25f, 0.25f), P("angle", -180, 180), P("radial"), P("mix")};
    case MaterialEffectKind::Dither: return {P("levels", 2, 64), P("cellSize", 0.001f, 0.125f), P("amount"), P("mix")};
    case MaterialEffectKind::Halftone:
        return {P("cellSize", 0.002f, 0.25f), P("angle", -180, 180), P("contrast", 0.01f, 4), P("mix")};
    case MaterialEffectKind::Duotone:
        return {P("balance", -1, 1), P("contrast", 0.01f, 4), P("preserveLight"), P("mix")};
    case MaterialEffectKind::Threshold:
        return {P("threshold", 0, 4), P("softness", 0.001f, 1), P("preserveLight"), P("mix")};
    case MaterialEffectKind::Vignette:
        return {P("amount"), P("radius", 0.01f, 2), P("softness", 0.001f, 1), P("roundness", 0.1f, 4)};
    case MaterialEffectKind::Grain: return {P("amount"), P("cellSize", 0.001f, 0.125f), P("seed", 0, 1000), P("mix")};
    case MaterialEffectKind::Scanlines:
        return {P("cellSize", 0.001f, 0.25f), P("amount"), P("phase", -100, 100), P("angle", -180, 180)};
    default: return {};
    }
}
std::array<MaterialEffectParameterInfo, 4> GetMaterialEffectParameters(const MaterialEffectStep& effect,
                                                                       bool secondary) noexcept {
    auto parameters = DescribeParameters(effect.kind, secondary);
    if (secondary && effect.kind == MaterialEffectKind::Wave && effect.parameters2.x > 0.5f)
        parameters[3].animatable = false;
    if (secondary && effect.kind == MaterialEffectKind::FlowMap && effect.parameters2.z <= 0.5f)
        parameters[1].animatable = false;
    return parameters;
}
std::string_view MaterialEffectName(MaterialEffectKind kind) noexcept {
    const size_t index = static_cast<size_t>(kind);
    return index < std::size(names) ? names[index] : "Invalid";
}
std::span<const std::string_view> GetMaterialEffectKindNames() noexcept { return names; }
std::string MaterialEffectKindList() {
    std::string result;
    for (const std::string_view name : names) {
        if (!result.empty())
            result += ", ";
        result += name;
    }
    return result;
}
bool ParseMaterialEffectKind(std::string_view text, MaterialEffectKind& kind) noexcept {
    for (size_t i = 0; i < std::size(names); ++i)
        if (names[i] == text) {
            kind = static_cast<MaterialEffectKind>(i);
            return true;
        }
    return false;
}
MaterialEffectStep MakeMaterialEffect(MaterialEffectKind kind, std::string id) {
    MaterialEffectStep step;
    step.id = std::move(id);
    step.kind = kind;
    switch (kind) {
    case MaterialEffectKind::FlowMap:
        step.parameters = {0.15f, 0.8f, 1, 1};
        step.textureTarget = MaterialEffectTextureTarget::BaseColor;
        break;
    case MaterialEffectKind::Pixelate: step.parameters = {0.024f, 1, 0, 0}; break;
    case MaterialEffectKind::Posterize: step.parameters = {5, 1, 0, 0}; break;
    case MaterialEffectKind::Ink:
        step.parameters = {0.006f, 1, 0.3f, 1};
        step.colorA = {0.015f, 0.02f, 0.03f, 1};
        step.parameters2 = {0, 1, 0, 0};
        break;
    // UvFlow: x/y=フロー速度, z/w=タイル, parameters2=回転/軸交換/ピボット。
    case MaterialEffectKind::UvFlow: step.parameters = {0.5f, 0, 1, 1}; break;
    case MaterialEffectKind::UvDistortion: step.parameters = {0.06f, 5, 0.2f, 0}; break;
    case MaterialEffectKind::Gradient:
        step.colorA = {0.05f, 0.15f, 0.8f, 1};
        step.colorB = {0.4f, 1.5f, 3, 1};
        // 既定は 2 ストップ。増やすと多段ランプとして評価される。
        step.colorStops.push_back({step.id + ".start", 0.0f, step.colorA});
        step.colorStops.push_back({step.id + ".end", 1.0f, step.colorB});
        break;
    case MaterialEffectKind::Fade: step.parameters = {0.08f, 0.35f, 0, 0}; break;
    case MaterialEffectKind::Wipe: step.parameters = {0, 1, 0.06f, 0}; break;
    case MaterialEffectKind::Dissolve:
        step.parameters = {0, 1, 8, 0.08f};
        step.colorA = {0.4f, 2, 4, 1};
        break;
    case MaterialEffectKind::Emission: step.parameters = {1.5f, 0.15f, 2, 0}; break;
    case MaterialEffectKind::Fresnel:
        step.parameters = {3, 2, 0.8f, 0};
        step.colorA = {0.1f, 0.7f, 2, 1};
        break;
    case MaterialEffectKind::Opacity: step.parameters = {1, 0, 0, 0}; break;
    // Glow: x=しきい値, y=強度, z=軟らかさ。ぼかしは Blur が担当する。
    case MaterialEffectKind::Glow:
        // x=しきい値, y=強度, z=軟らかさ, w=ぼかし半径(UV)。
        // w=0 は加算のみ（従来動作）。w>0 で十字 4 タップのぼかしが入るが、
        // マテリアル式の命令/レジスタ予算を消費するので既定では切っておく。
        // 光輪が要る層だけ明示的に有効化する。
        step.parameters = {0.75f, 1.5f, 0.15f, 0.0f};
        step.colorA = {1, 0.85f, 0.6f, 1};
        break;
    // Blur: x=半径(UV), y=方向(度), z=片側タップ数(1..4)。
    case MaterialEffectKind::Blur: step.parameters = {0.02f, 0, 2, 0}; break;
    // LightSweep: x=中心, y=幅, z=方向(度), w=強度。
    case MaterialEffectKind::LightSweep:
        step.parameters = {0.5f, 0.35f, 0, 1.5f};
        step.colorA = {1, 0.95f, 0.8f, 1};
        break;
    // LuminanceMask: x=しきい値, y=軟らかさ, z=0 乗算 / 1 置換。
    case MaterialEffectKind::LuminanceMask: step.parameters = {0.05f, 0.1f, 0, 0}; break;
    // RadialBlur: x=半径, y/z=中心 UV, w=片側タップ数。parameters2.x は
    // 正=外側へ / 負=中心へ引く方向。
    case MaterialEffectKind::RadialBlur:
        step.parameters = {0.06f, 0.5f, 0.5f, 2.0f};
        step.parameters2 = {1.0f, 0, 0, 0};
        break;
    // TurbulentDisplace: x=振幅, y=周波数, z=進化速度, w=第 2 オクターブの比率。
    case MaterialEffectKind::TurbulentDisplace: step.parameters = {0.04f, 6.0f, 0.6f, 0.45f}; break;
    // ColorCorrect: x=露出(乗算), y=コントラスト, z=彩度, w=黒レベル。
    case MaterialEffectKind::ColorCorrect: step.parameters = {1.0f, 1.0f, 1.0f, 0.0f}; break;
    // Streak: x=長さ(UV), y=角度(度), z=タップ数, w=減衰(0..1)。
    case MaterialEffectKind::Streak: step.parameters = {0.06f, 0.0f, 4.0f, 0.6f}; break;
    case MaterialEffectKind::Noise:
        step.parameters = {8, 0, 0.5f, 1};
        step.parameters2 = {0, 0, 1, 0};
        break;
    case MaterialEffectKind::Polar: step.parameters = {1, 0.5f, 0.5f, 1}; break;
    case MaterialEffectKind::Twirl: step.parameters = {90, 0.5f, 0.5f, 0.5f}; break;
    case MaterialEffectKind::Wave:
        step.parameters = {0.03f, 4, 0, 1};
        step.parameters2 = {1, 0.5f, 0.5f, 0};
        break;
    case MaterialEffectKind::AlphaMorph: step.parameters = {0.008f, 0, 1, 0}; break;
    case MaterialEffectKind::Outline: step.parameters = {0.008f, 0, 1, 1}; break;
    case MaterialEffectKind::Levels: step.parameters = {0, 1, 1, 1}; break;
    case MaterialEffectKind::ChannelMixer: step.parameters = {0.2126f, 0.7152f, 0.0722f, 1}; break;
    case MaterialEffectKind::Chromatic: step.parameters = {0.008f, 0, 0, 1}; break;
    case MaterialEffectKind::Dither: step.parameters = {5, 0.004f, 0.5f, 1}; break;
    case MaterialEffectKind::Halftone:
        step.parameters = {0.024f, 45, 1, 1};
        step.colorA = {0.01f, 0.015f, 0.02f, 1};
        break;
    case MaterialEffectKind::Duotone:
        step.parameters = {0, 1, 1, 1};
        step.colorA = {0.02f, 0.03f, 0.12f, 1};
        step.colorB = {0.8f, 0.5f, 0.15f, 1};
        break;
    case MaterialEffectKind::Threshold:
        step.parameters = {0.5f, 0.02f, 1, 1};
        step.colorA = {0, 0, 0, 1};
        break;
    case MaterialEffectKind::Vignette: step.parameters = {0.5f, 0.6f, 0.4f, 1}; break;
    case MaterialEffectKind::Grain: step.parameters = {0.15f, 0.004f, 0, 1}; break;
    case MaterialEffectKind::Scanlines: step.parameters = {0.012f, 0.25f, 0, 0}; break;
    default: break;
    }
    return step;
}

} // namespace HIKARI
