#pragma once

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironmentNames.h"

// 環境プロファイル（.henvironment）の節と項目の表。資産の読み書き・値の正規化・エディターの項目表示が共有する。
namespace HIKARI::ENVIRONMENT::FIELDS {

using JSONFIELDS::Range;

constexpr auto kSkyFields = [](auto&& field) {
    using S = SkySettings;
    field("source", &S::source, kSkySourceNames);
    field("asset", &S::asset);
    field("textureIlluminance", &S::textureIlluminance, Range{0.0, 200000.0});
    field("panoramaCloudAsset", &S::panoramaCloudAsset);
    field("panoramaCloudScrollSpeed", &S::panoramaCloudScrollSpeed, Range{-1.0, 1.0});
    field("panoramaCloudOpacity", &S::panoramaCloudOpacity, Range{0.0, 1.0});
    field("yaw", &S::yaw, Range{-360.0, 360.0});
    field("tint", &S::tint, Range{0.0, 16.0});
    field("lightIntensity", &S::lightIntensity, Range{0.0, 16.0});
    field("reflectionIntensity", &S::reflectionIntensity, Range{0.0, 16.0});
};

constexpr auto kAtmosphereFields = [](auto&& field) {
    using S = AtmosphereSettings;
    field("planetRadius", &S::planetRadius, Range{1000.0, 100000000.0});
    field("atmosphereHeight", &S::atmosphereHeight, Range{1000.0, 1000000.0});
    field("rayleighScaleHeight", &S::rayleighScaleHeight, Range{1.0, 1000000.0});
    field("mieScaleHeight", &S::mieScaleHeight, Range{1.0, 1000000.0});
    field("rayleighScattering", &S::rayleighScattering, Range{0.0, 0.01});
    field("mieScattering", &S::mieScattering, Range{0.0, 0.01});
    field("ozoneAbsorption", &S::ozoneAbsorption, Range{0.0, 0.01});
    field("ozoneCenterHeight", &S::ozoneCenterHeight, Range{0.0, 1000000.0});
    field("ozoneWidth", &S::ozoneWidth, Range{1.0, 1000000.0});
    field("mieAnisotropy", &S::mieAnisotropy, Range{-0.95, 0.95});
    field("groundAlbedo", &S::groundAlbedo, Range{0.0, 1.0});
    field("aerialPerspectiveStrength", &S::aerialPerspectiveStrength, Range{0.0, 8.0});
    field("transmittanceLutWidth", &S::transmittanceLutWidth, Range{32.0, 1024.0});
    field("transmittanceLutHeight", &S::transmittanceLutHeight, Range{16.0, 256.0});
    field("multiScatteringLutSize", &S::multiScatteringLutSize, Range{8.0, 64.0});
    field("skyViewLutWidth", &S::skyViewLutWidth, Range{32.0, 512.0});
    field("skyViewLutHeight", &S::skyViewLutHeight, Range{16.0, 512.0});
};

constexpr auto kSunFields = [](auto&& field) {
    using S = SunSettings;
    field("mode", &S::mode, kSunModeNames);
    field("fixedAzimuthDegrees", &S::fixedAzimuthDegrees, Range{0.0, 360.0});
    field("fixedElevationDegrees", &S::fixedElevationDegrees, Range{-90.0, 90.0});
    field("illuminance", &S::illuminance, Range{0.0, 1000000.0});
    field("color", &S::color, Range{0.0, 4.0});
    field("angularRadius", &S::angularRadius, Range{0.0001, 0.25});
    field("showDisk", &S::showDisk);
};

constexpr auto kMoonFields = [](auto&& field) {
    using S = MoonSettings;
    field("enabled", &S::enabled);
    field("illuminance", &S::illuminance, Range{0.0, 100.0});
    field("color", &S::color, Range{0.0, 4.0});
    field("angularRadius", &S::angularRadius, Range{0.0001, 0.25});
};

constexpr auto kStarFields = [](auto&& field) {
    using S = StarSettings;
    field("enabled", &S::enabled);
    field("intensity", &S::intensity, Range{0.0, 100.0});
    field("limitingMagnitude", &S::limitingMagnitude, Range{3.0, 9.0});
    field("backgroundLuminance", &S::backgroundLuminance, Range{0.0, 0.01});
};

// 被覆率・雲型・降水・風は天気が毎フレーム与えるので、プロファイルには持たない。
constexpr auto kCloudFields = [](auto&& field) {
    using S = CloudLayerSettings;
    field("enabled", &S::enabled);
    field("baseAltitude", &S::baseAltitude, Range{100.0, 10000.0});
    field("densityScale", &S::densityScale, Range{0.0, 8.0});
    field("shapeScale", &S::shapeScale, Range{0.25, 4.0});
    field("detailErosion", &S::detailErosion, Range{0.0, 1.0});
    field("ambientScale", &S::ambientScale, Range{0.0, 4.0});
};

constexpr auto kFogFields = [](auto&& field) {
    using S = FogSettings;
    field("enabled", &S::enabled);
    field("volumetric", &S::volumetric);
    field("albedo", &S::albedo, Range{0.0, 1.0});
    field("density", &S::density, Range{0.0, 1.0});
    field("startDistance", &S::startDistance, Range{0.0, 100000.0});
    field("heightFalloff", &S::heightFalloff, Range{0.0, 1.0});
    field("baseHeight", &S::baseHeight, Range{-100000.0, 100000.0});
    field("anisotropy", &S::anisotropy, Range{-0.95, 0.95});
    field("temporalWeight", &S::temporalWeight, Range{0.0, 0.98});
    field("scatteringIntensity", &S::scatteringIntensity, Range{0.0, 16.0});
    field("ambientScatteringStrength", &S::ambientScatteringStrength, Range{0.0, 16.0});
    field("sunShaftStrength", &S::sunShaftStrength, Range{0.0, 16.0});
    field("noise", &S::noise, Range{0.0, 1.0});
    field("noiseScale", &S::noiseScale, Range{1.0, 500.0});
};

constexpr auto kExposureFields = [](auto&& field) {
    using S = RENDER3D::COLOR::ExposureSettings;
    field("mode", &S::mode, RENDER3D::COLOR::kExposureModeNames);
    field("manualEv100", &S::manualEv100, Range{-10.0, 24.0});
    field("compensationEv", &S::compensationEv, Range{-8.0, 8.0});
    field("minEv100", &S::minEv100, Range{-10.0, 24.0});
    field("maxEv100", &S::maxEv100, Range{-10.0, 24.0});
    field("speedUp", &S::speedUp, Range{0.0, 20.0});
    field("speedDown", &S::speedDown, Range{0.0, 20.0});
    field("nightAdaptationEv100", &S::nightAdaptationEv100, Range{-10.0, 24.0});
    field("nightAdaptationStrength", &S::nightAdaptationStrength, Range{0.0, 1.0});
    field("highlightProtection", &S::highlightProtection, Range{0.0, 1.0});
    field("sunlitMeteringCorrectionEv", &S::sunlitMeteringCorrectionEv, Range{0.0, 4.0});
    field("dimAdaptationStrength", &S::dimAdaptationStrength, Range{0.0, 1.0});
};

constexpr auto kToneMappingFields = [](auto&& field) {
    using S = ToneMappingSettings;
    field("curve", &S::curve, kToneCurveNames);
    field("whiteBalanceTemperature", &S::whiteBalanceTemperature, Range{2000.0, 15000.0});
    field("whiteBalanceTint", &S::whiteBalanceTint, Range{-1.0, 1.0});
    field("contrast", &S::contrast, Range{0.25, 2.0});
    field("saturation", &S::saturation, Range{0.0, 2.0});
    field("gamma", &S::gamma, Range{0.01, 4.0});
};

constexpr auto kColorGradingFields = [](auto&& field) {
    using S = ColorGradingSettings;
    field("shadowsTint", &S::shadowsTint, Range{0.0, 2.0});
    field("midtonesTint", &S::midtonesTint, Range{0.0, 2.0});
    field("highlightsTint", &S::highlightsTint, Range{0.0, 2.0});
    field("lut", &S::lut);
    field("lutIntensity", &S::lutIntensity, Range{0.0, 1.0});
};

constexpr auto kBloomFields = [](auto&& field) {
    using S = BloomSettings;
    field("enabled", &S::enabled);
    field("threshold", &S::threshold, Range{0.0, 10.0});
    field("intensity", &S::intensity, Range{0.0, 5.0});
    field("radius", &S::radius, Range{0.0, 8.0});
    field("downsampleCount", &S::downsampleCount, Range{1.0, 6.0});
};

constexpr auto kSunGlareFields = [](auto&& field) {
    using S = SunGlareSettings;
    field("enabled", &S::enabled);
    field("intensity", &S::intensity, Range{0.0, 8.0});
    field("haloRadius", &S::haloRadius, Range{0.0, 2.0});
    field("haloFalloff", &S::haloFalloff, Range{0.1, 16.0});
    field("streakIntensity", &S::streakIntensity, Range{0.0, 8.0});
    field("streakLength", &S::streakLength, Range{0.0, 2.0});
    field("occlusionRadius", &S::occlusionRadius, Range{0.0, 0.25});
    field("edgeFade", &S::edgeFade, Range{0.0, 1.0});
    field("maxRadiance", &S::maxRadiance, Range{0.0, 64.0});
    field("shaftIntensity", &S::shaftIntensity, Range{0.0, 8.0});
    field("shaftLength", &S::shaftLength, Range{0.01, 2.0});
    field("shaftDecay", &S::shaftDecay, Range{0.5, 0.999});
    field("ghostIntensity", &S::ghostIntensity, Range{0.0, 4.0});
};

constexpr auto kAmbientOcclusionFields = [](auto&& field) {
    using S = AmbientOcclusionSettings;
    field("enabled", &S::enabled);
    field("mode", &S::mode, kSsaoModeNames);
    field("radius", &S::radius, Range{0.01, 8.0});
    field("bias", &S::bias, Range{0.0, 1.0});
    field("strength", &S::strength, Range{0.0, 4.0});
    field("power", &S::power, Range{0.1, 8.0});
    field("diffuseStrength", &S::diffuseStrength, Range{0.0, 1.0});
    field("specularStrength", &S::specularStrength, Range{0.0, 1.0});
    field("sampleCount", &S::sampleCount, Range{8.0, 32.0});
    field("blurIterations", &S::blurIterations, Range{0.0, 4.0});
};

constexpr auto kScreenSpaceReflectionFields = [](auto&& field) {
    using S = ScreenSpaceReflectionSettings;
    field("enabled", &S::enabled);
    field("quality", &S::quality, kScreenSpaceReflectionQualityNames);
    field("intensity", &S::intensity, Range{0.0, 2.0});
    field("maxDistance", &S::maxDistance, Range{1.0, 200.0});
    field("thickness", &S::thickness, Range{0.0001, 0.05});
    field("maxRoughness", &S::maxRoughness, Range{0.05, 1.0});
    field("edgeFade", &S::edgeFade, Range{0.01, 0.35});
    field("stride", &S::stride, Range{0.02, 2.0});
    field("rayTracingFallback", &S::rayTracingFallback);
};

constexpr auto kSubsurfaceScatteringFields = [](auto&& field) {
    using S = SubsurfaceScatteringSettings;
    field("enabled", &S::enabled);
    field("strength", &S::strength, Range{0.0, 2.0});
    field("channelFalloff", &S::channelFalloff, Range{0.05, 1.0});
    field("maxPixelRadius", &S::maxPixelRadius, Range{1.0, 256.0});
};

constexpr auto kShadowFields = [](auto&& field) {
    using S = ShadowSettings;
    field("enabled", &S::enabled);
    field("distance", &S::distance, Range{1.0, 10000.0});
    field("strength", &S::strength, Range{0.0, 1.0});
};

// 節ごとの読み書きを一か所に並べる。visit(key, member, fields)。
template <class Visitor>
void VisitProfileSections(Visitor&& visit) {
    visit("sky", &EnvironmentProfile::sky, kSkyFields);
    visit("atmosphere", &EnvironmentProfile::atmosphere, kAtmosphereFields);
    visit("sun", &EnvironmentProfile::sun, kSunFields);
    visit("moon", &EnvironmentProfile::moon, kMoonFields);
    visit("stars", &EnvironmentProfile::stars, kStarFields);
    visit("clouds", &EnvironmentProfile::clouds, kCloudFields);
    visit("fog", &EnvironmentProfile::fog, kFogFields);
    visit("exposure", &EnvironmentProfile::exposure, kExposureFields);
    visit("toneMapping", &EnvironmentProfile::toneMapping, kToneMappingFields);
    visit("colorGrading", &EnvironmentProfile::colorGrading, kColorGradingFields);
    visit("bloom", &EnvironmentProfile::bloom, kBloomFields);
    visit("sunGlare", &EnvironmentProfile::sunGlare, kSunGlareFields);
    visit("ambientOcclusion", &EnvironmentProfile::ambientOcclusion, kAmbientOcclusionFields);
    visit("screenSpaceReflections", &EnvironmentProfile::screenSpaceReflections, kScreenSpaceReflectionFields);
    visit("subsurfaceScattering", &EnvironmentProfile::subsurfaceScattering, kSubsurfaceScatteringFields);
    visit("shadows", &EnvironmentProfile::shadows, kShadowFields);
}

} // namespace HIKARI::ENVIRONMENT::FIELDS
