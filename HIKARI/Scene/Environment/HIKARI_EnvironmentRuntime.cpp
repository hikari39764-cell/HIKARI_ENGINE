#include "Scene/Environment/HIKARI_EnvironmentRuntime.h"
#include "Core/Math/HIKARI_ScalarMath.h"

#include <algorithm>
#include <cmath>

#include "Assets/Environment/HIKARI_EnvironmentFields.h"
#include "Core/Hashing/HIKARI_FnvHash.h"

namespace HIKARI::ENVIRONMENT {
namespace {

// 太陽がこの高度より沈むと月が影の主光源になる（市民薄明の終わり。それまでは太陽が空と高い雲を照らす）。
constexpr float kMoonPrimarySunAltitudeDegrees = -6.0f;

uint64_t WeatherKey(std::string_view weatherId) noexcept {
    return HASH::Fnv1aBytes(HASH::kFnv1a64OffsetBasis, weatherId.data(), weatherId.size());
}

// 地平線の下では方向光を消す。大気の透過率は描画側が掛けるので、ここは幾何的な遮蔽だけ。
float HorizonFade(float altitudeDegrees, float angularRadius) noexcept {
    const float radiusDegrees = angularRadius * 57.2957795f;
    return MATH::SmoothStep(-radiusDegrees, radiusDegrees, altitudeDegrees);
}

} // namespace

void ApplyWeather(EnvironmentProfile& profile, const WeatherParameters& weather) {
    // 天気の霧は基本の高さ霧に加わる。高さ減衰は濃度の重みで混ぜる。
    if (weather.fogDensity > 0.0f) {
        const float baseDensity = profile.fog.enabled ? profile.fog.density : 0.0f;
        const float total = baseDensity + weather.fogDensity;
        profile.fog.heightFalloff =
            (baseDensity * profile.fog.heightFalloff + weather.fogDensity * weather.fogHeightFalloff) / total;
        profile.fog.density = total;
        profile.fog.enabled = true;
    }
    // 霞はエアロゾル（Mie 散乱）の増加として扱う。
    profile.atmosphere.mieScattering = profile.atmosphere.mieScattering * (1.0f + 4.0f * weather.haze);
}

EnvironmentLightingFrame BuildLightingFrame(const EnvironmentProfile& profile, const EnvironmentFrameState& state,
                                            const GeoLocation& location) {
    EnvironmentLightingFrame frame{};
    const CelestialState& celestial = state.celestial;
    float sunAltitude = celestial.sunAltitudeDegrees;
    frame.sun.toLight = celestial.sunDirection;
    if (profile.sun.mode == SunMode::Fixed) {
        sunAltitude = profile.sun.fixedElevationDegrees;
        frame.sun.toLight = HorizonDirectionToWorld(profile.sun.fixedAzimuthDegrees, profile.sun.fixedElevationDegrees,
                                                    location.northYawDegrees);
    }
    frame.sun.angularRadius = profile.sun.angularRadius;
    frame.sun.color = profile.sun.color;
    frame.sun.illuminance = profile.sun.illuminance;
    frame.sun.horizonVisibility = HorizonFade(sunAltitude, profile.sun.angularRadius);

    frame.moon.toLight = celestial.moonDirection;
    frame.moon.angularRadius = profile.moon.angularRadius;
    frame.moon.color = profile.moon.color;
    frame.moon.illuminance = profile.moon.enabled ? profile.moon.illuminance * celestial.moonBrightness : 0.0f;
    frame.moon.horizonVisibility = HorizonFade(celestial.moonAltitudeDegrees, profile.moon.angularRadius);
    frame.moonIsPrimary = profile.sun.mode == SunMode::Celestial && sunAltitude < kMoonPrimarySunAltitudeDegrees &&
                          frame.moon.illuminance * frame.moon.horizonVisibility > 0.0f;

    frame.celestialToWorldX = celestial.celestialToWorldX;
    frame.celestialToWorldY = celestial.celestialToWorldY;
    frame.celestialToWorldZ = celestial.celestialToWorldZ;
    frame.surfaceWetness = state.surface.wetness;
    frame.surfacePuddles = state.surface.puddles;
    frame.surfaceSnowCover = state.surface.snowCover;
    frame.lightningFlash = state.lightningFlash;
    frame.lightningAge = state.lightningAge;
    const MATH::Vec3 lightningDirection =
        HorizonDirectionToWorld(state.lightningAzimuthDegrees, 0.0f, location.northYawDegrees);
    frame.lightningDirection = {lightningDirection.x, lightningDirection.z};
    frame.lightningDistance = state.lightningDistance;
    frame.localHours = static_cast<float>(state.time.localHours);
    const WeatherParameters& weather = state.weather;
    frame.clouds.coverage = weather.cloudCoverage;
    frame.clouds.type = weather.cloudType;
    frame.clouds.density = weather.cloudDensity;
    frame.clouds.precipitation =
        weather.precipitation != PrecipitationType::None ? weather.precipitationIntensity : 0.0f;
    frame.clouds.windVelocity = state.windVelocity;
    frame.clouds.windDisplacementX = state.windDisplacementX;
    frame.clouds.windDisplacementZ = state.windDisplacementZ;
    frame.clouds.seconds = state.cloudSeconds;
    return frame;
}

void EnvironmentRuntime::Reset(const SceneEnvironmentBinding& binding) {
    binding_ = binding;
    colorGradingOverride_.reset();
    RefreshProfile();
    renderStyleId_ = binding_.renderStyleId;
    weatherId_ = binding_.weatherId.empty() ? profile_.defaultWeather : binding_.weatherId;
    WeatherParameters weather{};
    std::string weatherError;
    if (store_ != nullptr) {
        if (const auto asset = store_->LoadWeather(weatherId_, weatherError)) {
            weather = asset->weather;
        } else if (error_.empty()) {
            error_ = weatherError;
        }
    }
    simulation_.Reset(binding_.time, binding_.location, weather, WeatherKey(weatherId_), binding_.timeScale);
}

void EnvironmentRuntime::RefreshProfile() {
    error_.clear();
    profile_ = {};
    colorLut_.reset();
    if (store_ == nullptr) {
        error_ = "Environment assets are not configured.";
        return;
    }
    if (const auto asset = store_->LoadProfile(binding_.profileId, error_)) {
        profile_ = asset->profile;
        if (colorGradingOverride_) {
            profile_.colorGrading = *colorGradingOverride_;
        }
        if (!profile_.colorGrading.lut.empty()) {
            colorLut_ = store_->LoadColorLut(profile_.colorGrading.lut, error_);
        }
    }
}

void EnvironmentRuntime::Advance(float deltaSeconds) { simulation_.Advance(deltaSeconds); }

SceneEnvironment EnvironmentRuntime::ResolveFrame() const {
    SceneEnvironment environment{};
    static_cast<EnvironmentProfile&>(environment) = profile_;
    const EnvironmentFrameState state = simulation_.GetFrameState();
    const GeoLocation& location = simulation_.GetSnapshot().location;
    ApplyWeather(environment, state.weather);
    environment.frame = BuildLightingFrame(profile_, state, location);
    return environment;
}

bool EnvironmentRuntime::RequestWeather(std::string_view weatherId, float seconds, std::string& error) {
    if (store_ == nullptr) {
        error = "Environment assets are not configured.";
        return false;
    }
    const auto asset = store_->LoadWeather(weatherId, error);
    if (!asset) {
        return false;
    }
    weatherId_ = weatherId;
    simulation_.RequestWeather(asset->weather, WeatherKey(weatherId), seconds);
    return true;
}

bool EnvironmentRuntime::SetColorGradingOverride(const ColorGradingSettings& grading, std::string& error) {
    const ColorGradingSettings normalized = JSONFIELDS::Sanitize(grading, FIELDS::kColorGradingFields);
    if (!normalized.lut.empty() && (store_ == nullptr || !store_->LoadColorLut(normalized.lut, error))) {
        if (store_ == nullptr) {
            error = "Environment assets are not configured.";
        }
        return false;
    }
    colorGradingOverride_ = normalized;
    RefreshProfile();
    return true;
}

void EnvironmentRuntime::ClearColorGradingOverride() {
    if (colorGradingOverride_) {
        colorGradingOverride_.reset();
        RefreshProfile();
    }
}

bool EnvironmentRuntime::Restore(const EnvironmentSnapshot& snapshot, std::string_view weatherId,
                                 std::string_view renderStyleId) {
    if (renderStyleId.empty() || snapshot.weather.targetId != WeatherKey(weatherId) || !simulation_.Restore(snapshot)) {
        return false;
    }
    weatherId_ = weatherId;
    renderStyleId_ = renderStyleId;
    return true;
}

} // namespace HIKARI::ENVIRONMENT
