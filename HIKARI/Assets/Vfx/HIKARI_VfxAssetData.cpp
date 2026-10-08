#include "Assets/Vfx/HIKARI_VfxAssetData.h"

#include <cmath>

#include "Assets/Vfx/HIKARI_VfxEnumNames.h"

namespace HIKARI::VFX {
bool IsValidVfxSpecialOutputProfile(const VfxSpecialOutputData& output) noexcept {
    const auto inRange = [](auto value, auto minimum, auto maximum) noexcept {
        return std::isfinite(static_cast<double>(value)) && value >= minimum && value <= maximum;
    };
    const auto finiteColor = [](const MATH::Vec4& color) noexcept {
        return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z) && std::isfinite(color.w);
    };
    const auto positiveColor = [&](const MATH::Vec4& color) noexcept {
        return finiteColor(color) && color.x >= 0 && color.y >= 0 && color.z >= 0 && color.w >= 0;
    };
    switch (output.type) {
    case VfxSpecialOutputType::WeaponPathDistortion: {
        const auto& profile = output.weaponPathDistortion;
        return inRange(profile.distortion, kVfxWeaponDistortionMin, kVfxWeaponDistortionMax) &&
               inRange(profile.blur, kVfxWeaponBlurMin, kVfxWeaponBlurMax) &&
               inRange(profile.opacity, kVfxWeaponOpacityMin, kVfxWeaponOpacityMax) &&
               inRange(profile.emissive, kVfxWeaponEmissiveMin, kVfxWeaponEmissiveMax) &&
               inRange(profile.flow, kVfxWeaponFlowMin, kVfxWeaponFlowMax) &&
               inRange(profile.width, kVfxWeaponWidthMin, kVfxWeaponWidthMax) &&
               inRange(profile.lifetimeSeconds, kVfxWeaponLifetimeMin, kVfxWeaponLifetimeMax) &&
               positiveColor(profile.bodyColor) && positiveColor(profile.edgeColor);
    }
    case VfxSpecialOutputType::ScreenImpulse: {
        const auto& profile = output.screenImpulse;
        return inRange(profile.durationSeconds, kVfxScreenDurationMin, kVfxScreenDurationMax) &&
               inRange(profile.maxRadius, kVfxScreenRadiusMin, kVfxScreenRadiusMax) &&
               inRange(profile.ringThickness, kVfxScreenRingThicknessMin, kVfxScreenRingThicknessMax) &&
               inRange(profile.distortion, kVfxScreenDistortionMin, kVfxScreenDistortionMax) &&
               inRange(profile.chromaticAberration, kVfxScreenChromaticMin, kVfxScreenChromaticMax) &&
               inRange(profile.flash, kVfxScreenFlashMin, kVfxScreenFlashMax) &&
               inRange(profile.intensity, kVfxScreenIntensityMin, kVfxScreenIntensityMax) &&
               inRange(profile.waveFrequency, kVfxScreenWaveFrequencyMin, kVfxScreenWaveFrequencyMax) &&
               inRange(profile.waveAmplitude, kVfxScreenWaveAmplitudeMin, kVfxScreenWaveAmplitudeMax) &&
               inRange(profile.waveDamping, kVfxScreenWaveDampingMin, kVfxScreenWaveDampingMax) &&
               inRange(profile.waveCount, kVfxScreenImpulseWaveCountMin, kVfxScreenImpulseWaveCountMax) &&
               positiveColor(profile.color);
    }
    case VfxSpecialOutputType::SkinnedAfterimage: {
        const auto& profile = output.skinnedAfterimage;
        return inRange(profile.lifetimeSeconds, kVfxAfterimageLifetimeMin, kVfxAfterimageLifetimeMax) &&
               inRange(profile.opacity, kVfxAfterimageOpacityMin, kVfxAfterimageOpacityMax) &&
               inRange(profile.fresnel, kVfxAfterimageFresnelMin, kVfxAfterimageFresnelMax) &&
               inRange(profile.dissolve, kVfxAfterimageDissolveMin, kVfxAfterimageDissolveMax) &&
               inRange(profile.shellOffset, kVfxAfterimageShellOffsetMin, kVfxAfterimageShellOffsetMax) &&
               inRange(profile.intensity, kVfxAfterimageIntensityMin, kVfxAfterimageIntensityMax) &&
               inRange(profile.sampleCount, kVfxAfterimageSampleCountMin, kVfxAfterimageSampleCountMax) &&
               positiveColor(profile.color);
    }
    case VfxSpecialOutputType::Light: {
        const auto& light = output.light;
        return finiteColor(light.color) && std::isfinite(light.luminousFlux) && light.luminousFlux >= 0 &&
               std::isfinite(light.range) && light.range > 0 && std::isfinite(light.sourceRadius) &&
               light.sourceRadius >= 0 && std::isfinite(light.flickerFrequency) &&
               std::isfinite(light.flickerAmplitude);
    }
    default: return false;
    }
}

bool IsValidVfxHistory(const VfxHistoryData& h) noexcept {
    return static_cast<uint32_t>(h.mode) <= 2u && std::isfinite(h.duration) && h.duration >= 0.01f &&
           h.duration <= 2.0f && h.samples >= 2u && h.samples <= 16u && std::isfinite(h.breakDistance) &&
           h.breakDistance > 0.0f && h.breakDistance <= 10000.0f && std::isfinite(h.tailWidth) && h.tailWidth >= 0 &&
           h.tailWidth <= 4 && std::isfinite(h.fadePower) && h.fadePower >= 0.05f && h.fadePower <= 8 &&
           std::isfinite(h.tailColor.x) && std::isfinite(h.tailColor.y) && std::isfinite(h.tailColor.z) &&
           std::isfinite(h.tailColor.w) && h.tailColor.x >= 0 && h.tailColor.y >= 0 && h.tailColor.z >= 0 &&
           h.tailColor.w >= 0 && h.tailColor.w <= 1;
}

// 範囲外の値は各列挙の既定値の名前として書く。
std::string_view ToString(VfxValueType value) noexcept { return TEXT::EnumToName(kVfxValueTypeNames, value, "Float"); }
std::string_view ToString(VfxShapeType value) noexcept { return TEXT::EnumToName(kVfxShapeTypeNames, value, "Point"); }
std::string_view ToString(VfxRenderShapeType value) noexcept {
    return TEXT::EnumToName(kVfxRenderShapeTypeNames, value, "Sprite");
}
std::string_view ToString(VfxShapeUvMode value) noexcept {
    return TEXT::EnumToName(kVfxShapeUvModeNames, value, "Parameter");
}
std::string_view ToString(VfxBlendMode value) noexcept { return TEXT::EnumToName(kVfxBlendModeNames, value, "Alpha"); }
std::string_view ToString(VfxPostProcessMode value) noexcept {
    return TEXT::EnumToName(kVfxPostProcessModeNames, value, "Scene");
}
std::string_view ToString(VfxMaterialMode value) noexcept {
    return TEXT::EnumToName(kVfxMaterialModeNames, value, "StandardUnlit");
}
std::string_view ToString(VfxParticleDepthMode value) noexcept {
    return TEXT::EnumToName(kVfxParticleDepthModeNames, value, "WorldOccluded");
}
std::string_view ToString(VfxParticleCapacityMode value) noexcept {
    return TEXT::EnumToName(kVfxParticleCapacityModeNames, value, "Automatic");
}
std::string_view ToString(VfxBillboardMode value) noexcept {
    return TEXT::EnumToName(kVfxBillboardModeNames, value, "Fixed");
}
std::string_view ToString(VfxBillboardAxis value) noexcept {
    return TEXT::EnumToName(kVfxBillboardAxisNames, value, "LocalX");
}
std::string_view ToString(VfxCurveInterpolation value) noexcept {
    return TEXT::EnumToName(kVfxCurveInterpolationNames, value, "Linear");
}
std::string_view ToString(VfxDistributionMode value) noexcept {
    return TEXT::EnumToName(kVfxDistributionModeNames, value, "Constant");
}
std::string_view ToString(VfxAnimatedValueMode value) noexcept {
    return TEXT::EnumToName(kVfxAnimatedValueModeNames, value, "Fixed");
}
std::string_view ToString(VfxAnimatedColorMode value) noexcept {
    return TEXT::EnumToName(kVfxAnimatedColorModeNames, value, "Fixed");
}
std::string_view ToString(VfxEasingFunction value) noexcept {
    return TEXT::EnumToName(kVfxEasingFunctionNames, value, "Linear");
}
std::string_view ToString(VfxAttachmentMode value) noexcept {
    return TEXT::EnumToName(kVfxAttachmentModeNames, value, "World");
}
std::string_view ToString(VfxFollowMode value) noexcept { return TEXT::EnumToName(kVfxFollowModeNames, value, "None"); }
std::string_view ToString(VfxSpecialOutputType value) noexcept {
    return TEXT::EnumToName(kVfxSpecialOutputTypeNames, value, "WeaponPathDistortion");
}
std::string_view ToString(VfxLayerKind value) noexcept {
    return TEXT::EnumToName(kVfxLayerKindNames, value, "Particles");
}
std::string_view ToString(VfxHistoryMode value) noexcept {
    return TEXT::EnumToName(kVfxHistoryModeNames, value, "None");
}
std::string_view ToString(VfxConnectTargetMode value) noexcept {
    return TEXT::EnumToName(kVfxConnectTargetModeNames, value, "None");
}

VfxFloatDistributionData MakeVfxFloatConstant(float value) noexcept {
    return {VfxDistributionMode::Constant, value, value, 0u};
}

VfxVec3DistributionData MakeVfxVec3Constant(const MATH::Vec3& value) noexcept {
    return {VfxDistributionMode::Constant, value, value, 0u};
}

VfxColorDistributionData MakeVfxColorConstant(const MATH::Vec4& value) noexcept {
    return {VfxDistributionMode::Constant, value, value, 0u};
}

VfxCurveData MakeVfxConstantCurve(std::string_view idPrefix, float value) {
    VfxCurveData curve{};
    curve.randomOffset = MakeVfxFloatConstant(0.0f);
    curve.keys = {
        {std::string(idPrefix) + ".start", 0.0f, value, 0.0f, 0.0f},
        {std::string(idPrefix) + ".end", 1.0f, value, 0.0f, 0.0f},
    };
    return curve;
}

VfxValue MakeDefaultVfxValue(VfxValueType type) noexcept {
    VfxValue value{};
    value.type = type;
    if (type == VfxValueType::Color) {
        value.numeric = {1.0f, 1.0f, 1.0f, 1.0f};
    }
    return value;
}

VfxAssetData MakeDefaultVfxAssetData() {
    const MATH::Vec3 zero{0.0f, 0.0f, 0.0f};
    const MATH::Vec3 one{1.0f, 1.0f, 1.0f};
    const MATH::Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};

    VfxAssetData asset{};
    VfxEmitterData emitter{};
    emitter.id = "emitter.main";
    emitter.name = "Main";
    emitter.basic.id = "module.basic";
    emitter.basic.lifetime = MakeVfxFloatConstant(1.0f);
    emitter.emitterTransform.id = "module.emitter_transform";
    emitter.attachment.id = "module.attachment";
    emitter.spawn.id = "module.spawn";
    emitter.shape.id = "module.shape";
    emitter.shape.radius = MakeVfxFloatConstant(0.0f);
    emitter.position.id = "module.position";
    emitter.position.radialSpeed = MakeVfxFloatConstant(1.0f);
    emitter.position.value.fixed = MakeVfxVec3Constant(zero);
    emitter.rotation.id = "module.rotation";
    emitter.rotation.eulerDegrees.fixed = MakeVfxVec3Constant(zero);
    emitter.scale.id = "module.scale";
    emitter.scale.value.fixed = MakeVfxVec3Constant(one);
    emitter.forces.id = "module.forces";
    emitter.color.id = "module.color";
    emitter.color.value.mode = VfxAnimatedColorMode::Gradient;
    emitter.color.value.fixed = MakeVfxColorConstant(white);
    emitter.color.value.gradient.stops = {
        {"color.start", 0.0f, white},
        {"color.end", 1.0f, {1.0f, 1.0f, 1.0f, 0.0f}},
    };
    emitter.renderCommon.id = "module.render_common";
    // 新規 VFX もプリセットと同じテクスチャ付き StandardUnlit 経路で
    // 直ちに確認できるよう、汎用のソフトディスクを初期値にする。
    emitter.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture.soft_disc"};
    emitter.renderShape.id = "module.render_shape";
    asset.emitters.push_back(std::move(emitter));
    return asset;
}

} // namespace HIKARI::VFX
