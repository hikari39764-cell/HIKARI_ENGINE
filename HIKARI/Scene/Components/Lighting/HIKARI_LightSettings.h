#pragma once

#include <cstdint>
#include <string>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

namespace HIKARI {

// 光束 [lm] の上限と既定値（白熱電球 60W 相当）。
inline constexpr float kMaximumLightLumens = 1000000.0f;
inline constexpr float kDefaultLightLumens = 800.0f;
// 管の長さ・長方形の辺 [m] の上限。
inline constexpr float kMaximumAreaLightExtent = 100.0f;

// LightComponent の作者値。シーン文書の properties と同じ形で、コンポーネントと焼き込みの読み取りが共有する。
struct LightSettings {
    bool enabled = true;
    RENDER3D::LIGHTING::SceneLightType type = RENDER3D::LIGHTING::SceneLightType::Point;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    // 光束 [lm]。描画は光度 I = Φ / 4π [cd]。
    float intensity = kDefaultLightLumens;
    float range = 10.0f;
    // 点・スポットは発光球の半径、管は管の太さ（半径）[m]。
    float sourceRadius = 0.0f;
    // 管の長さ、長方形の幅（ローカル X）[m]。
    float length = 1.0f;
    // 長方形の高さ（ローカル Y）[m]。発光面はローカル +Z を向く。
    float height = 1.0f;
    bool castsShadows = true;
    float innerConeAngleDegrees = 25.0f;
    float outerConeAngleDegrees = 35.0f;
    float angularFalloff = 1.0f;
    int priority = 0;
    // 点・スポットの配光（LightProfile 資産の GUID、空は等方）。鉛直角 0° がローカル +Z。
    std::string iesProfile;
    // スポットの投影模様（Texture 資産の GUID、空はなし）。円錐の外側の角で縁まで広がる。
    std::string cookie;

    bool operator==(const LightSettings&) const = default;
};

inline constexpr auto kLightSettingsFields = [](auto&& field) {
    using S = LightSettings;
    using JSONFIELDS::Range;
    field("enabled", &S::enabled);
    field("type", &S::type, RENDER3D::LIGHTING::kSceneLightTypeNames);
    field("color", &S::color, Range{0.0, 1.0});
    field("intensity", &S::intensity, Range{0.0, kMaximumLightLumens});
    field("range", &S::range, Range{0.05, 1000.0});
    field("sourceRadius", &S::sourceRadius, Range{0.0, RENDER3D::LIGHTING::kMaximumLocalLightSourceRadius});
    field("length", &S::length, Range{0.01, kMaximumAreaLightExtent});
    field("height", &S::height, Range{0.01, kMaximumAreaLightExtent});
    field("castsShadows", &S::castsShadows);
    field("innerConeAngleDegrees", &S::innerConeAngleDegrees, Range{0.1, 89.0});
    field("outerConeAngleDegrees", &S::outerConeAngleDegrees, Range{0.1, 89.0});
    field("angularFalloff", &S::angularFalloff, Range{0.05, 8.0});
    field("priority", &S::priority);
    field("iesProfile", &S::iesProfile);
    field("cookie", &S::cookie);
};

// 範囲へ丸め、外側の円錐角を内側以上にする。
LightSettings NormalizeLightSettings(LightSettings settings) noexcept;
LightSettings ReadLightSettings(const nlohmann::json& properties);
nlohmann::json WriteLightSettings(const LightSettings& settings);

} // namespace HIKARI
