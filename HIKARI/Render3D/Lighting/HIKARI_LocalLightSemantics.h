#pragma once

#include <cstdint>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::LIGHTING {

struct SceneLightProvider;

// 光束 [lm] から全方向に一様な光度 [cd] へ（I = Φ / 4π）。スポットも同じ換算で、円錐は遮光だけを行う。
float LuminousIntensityFromFlux(float lumens) noexcept;

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

bool IsLocalLightUploadable(const SceneLightProvider& light) noexcept;

MATH::Vec3 ResolveSpotLightDirection(const SceneLightProvider& light) noexcept;
SpotLightCone ResolveSpotLightCone(const SceneLightProvider& light) noexcept;

} // namespace HIKARI::RENDER3D::LIGHTING
