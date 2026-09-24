#pragma once

#include <cstdint>
#include <string_view>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {
struct PointLight;
struct SpotLight;
} // namespace HIKARI

namespace HIKARI::RENDER3D::LIGHTING {

// GPU と保存データで共有する値。既存シーンの未指定値は LegacyRange。
enum class LocalLightFalloff : uint32_t {
    LegacyRange = 0u,
    InverseSquare = 1u,
};

constexpr LocalLightFalloff CanonicalLocalLightFalloff(LocalLightFalloff value) noexcept {
    return value == LocalLightFalloff::InverseSquare ? value : LocalLightFalloff::LegacyRange;
}

constexpr const char* ToString(LocalLightFalloff value) noexcept {
    return value == LocalLightFalloff::InverseSquare ? "InverseSquare" : "LegacyRange";
}

constexpr LocalLightFalloff ParseLocalLightFalloff(std::string_view value) noexcept {
    return value == "InverseSquare" ? LocalLightFalloff::InverseSquare : LocalLightFalloff::LegacyRange;
}

struct SceneLightProvider;

// ワールド単位の発光体半径。影響範囲 range とは独立した値。
inline constexpr float kMaximumLocalLightSourceRadius = 1000.0f;
float SanitizeLocalLightSourceRadius(float radius) noexcept;

struct SpotLightCone {
    float innerAngleDegrees = 25.0f;
    float outerAngleDegrees = 35.0f;
    float innerCos = 0.0f;
    float outerCos = 0.0f;
    float angularFalloff = 1.0f;
};

bool IsLocalLightUploadable(const PointLight& light) noexcept;
bool IsLocalLightUploadable(const SpotLight& light) noexcept;
bool IsLocalLightUploadable(const SceneLightProvider& light) noexcept;

MATH::Vec3 ResolveSpotLightDirection(const SpotLight& light) noexcept;
SpotLightCone ResolveSpotLightCone(const SpotLight& light) noexcept;
MATH::Vec3 ResolveSpotLightDirection(const SceneLightProvider& light) noexcept;
SpotLightCone ResolveSpotLightCone(const SceneLightProvider& light) noexcept;
void SanitizeSpotLight(SpotLight& light) noexcept;

} // namespace HIKARI::RENDER3D::LIGHTING
