#pragma once

#include <cstdint>
#include <string>

#include "RenderContracts/Asset/HIKARI_AssetTypeEnum.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

namespace HIKARI {

// 光束 [lm] の上限と既定値（白熱電球 60W 相当）。
inline constexpr float kMaximumLightLumens = 1000000.0f;
inline constexpr float kDefaultLightLumens = 800.0f;
// 相対の明るさの上限と、場面がまだ描かれていない時の基準照度（晴天の昼の水平面）[lx]。
inline constexpr float kMaximumSceneRelativeIntensity = 100.0f;
inline constexpr float kDefaultSceneKeyIlluminance = 100000.0f;

// 明るさの単位。物理は光束 [lm] で、路灯や室内灯など実在の光源に使う（昼は露出に負けて見えないのが正しい）。
// 相対は環境（太陽と空）の照度に対する倍率で、補助光や演出の光に使う。昼夜や预设が変わっても場面に対する明るさを保つ。
enum class LightIntensityUnit : uint8_t {
    Lumens,
    SceneRelative,
};

inline constexpr TEXT::EnumName<LightIntensityUnit> kLightIntensityUnitNames[] = {
    {LightIntensityUnit::Lumens, "Lumens"},
    {LightIntensityUnit::SceneRelative, "SceneRelative"},
};
// 管の長さ・長方形の辺 [m] の上限。
inline constexpr float kMaximumAreaLightExtent = 100.0f;

// LightComponent の作者値。シーン文書の properties と同じ形で、コンポーネントと焼き込みの読み取りが共有する。
struct LightSettings {
    bool enabled = true;
    RENDER3D::LIGHTING::SceneLightType type = RENDER3D::LIGHTING::SceneLightType::Point;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    LightIntensityUnit intensityUnit = LightIntensityUnit::Lumens;
    // 物理の時の光束 [lm]。描画は光度 I = Φ / 4π [cd]。
    float intensity = kDefaultLightLumens;
    // 相対の時の倍率：1 m 先の照度が環境の照度の何倍か。
    float sceneRelativeIntensity = 1.0f;
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

// 保存・読み取り・検査器・説明が共有する項目表。並びは検査器の並び（Common が先頭、Advanced は最後の折りたたみ）。
inline constexpr auto kLightSettingsFields = [](auto&& field) {
    using S = LightSettings;
    using RENDER3D::LIGHTING::SceneLightType;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});
    field("type", &S::type, RENDER3D::LIGHTING::kSceneLightTypeNames, Label{"Type"}, Common{});
    field("color", &S::color, Range{0.0, 1.0}, Label{"Color"}, Color{}, Common{});
    field("intensityUnit", &S::intensityUnit, kLightIntensityUnitNames, Label{"Brightness Unit"},
          Help{"Physical: real lamps in lumens; they fade out in daylight as real lamps do. Relative: a multiple of "
               "the sun and sky light, so fill and accent lights keep their look across time of day, weather and "
               "presets."},
          Common{});
    field("intensity", &S::intensity, Range{0.0, kMaximumLightLumens}, Label{"Luminous Flux"}, Unit{"lm"},
          Help{"Total emitted light. A 60 W bulb is about 800 lm. Spot cones mask, they do not concentrate the flux; "
               "rect lights emit it from the front face only."},
          Common{}, Show{[](const S& s) { return s.intensityUnit == LightIntensityUnit::Lumens; }});
    field("sceneRelativeIntensity", &S::sceneRelativeIntensity, Range{0.0, kMaximumSceneRelativeIntensity},
          Label{"Brightness (x scene light at 1 m)"},
          Help{"1 lights a surface 1 m away as brightly as the sun and sky light the open ground."}, Common{},
          Show{[](const S& s) { return s.intensityUnit == LightIntensityUnit::SceneRelative; }});
    field("range", &S::range, Range{0.05, 1000.0}, Label{"Range"}, Unit{"m"},
          Help{"Inverse-square falloff fades smoothly to zero at this distance."}, Common{});
    field("castsShadows", &S::castsShadows, Label{"Cast Shadows"},
          Help{"Request local-light shadows within the active rendering budget."}, Common{});
    field("sourceRadius", &S::sourceRadius, Range{0.0, RENDER3D::LIGHTING::kMaximumLocalLightSourceRadius},
          Label{"Source Radius"},
          Help{"Emitter radius in metres. Broadens direct specular independently of material roughness and light "
               "range. Zero preserves punctual lighting."},
          Show{[](const S& s) { return s.type != SceneLightType::Rect; }});
    field("length", &S::length, Range{0.01, kMaximumAreaLightExtent}, Label{"Length / Width"}, Unit{"m"},
          Help{"Tube length or rect width, along local X."},
          Show{[](const S& s) { return s.type == SceneLightType::Tube || s.type == SceneLightType::Rect; }});
    field("height", &S::height, Range{0.01, kMaximumAreaLightExtent}, Label{"Height"}, Unit{"m"},
          Help{"Rect height, along local Y. The front face looks along +Z."},
          Show{[](const S& s) { return s.type == SceneLightType::Rect; }});
    const auto spot = [](const S& s) { return s.type == SceneLightType::Spot; };
    field("innerConeAngleDegrees", &S::innerConeAngleDegrees, Slider(0.1, 89.0), Label{"Inner Cone Angle"},
          Group{"Spot Cone (+Z forward)"}, Show{spot});
    field("outerConeAngleDegrees", &S::outerConeAngleDegrees, Slider(0.1, 89.0), Label{"Outer Cone Angle"},
          Group{"Spot Cone (+Z forward)"}, Show{spot});
    field("angularFalloff", &S::angularFalloff, Range{0.05, 8.0}, Label{"Angular Falloff"},
          Group{"Spot Cone (+Z forward)"}, Show{spot});
    field("cookie", &S::cookie, Label{"Cookie Texture"},
          Help{"Projected colour pattern multiplied into the light. It fills the outer cone and reads unmirrored from "
               "behind the light (texture up = local +Y)."},
          AssetRef{AssetType::Texture}, Group{"Spot Cone (+Z forward)"}, Show{spot});
    field("iesProfile", &S::iesProfile, Label{"IES Profile"},
          Help{"Photometric distribution (.ies, Type C). Vertical 0 degrees points along local +Z, horizontal 0 "
               "degrees along local +X. The flux setting stays the total output."},
          AssetRef{AssetType::LightProfile},
          Show{[](const S& s) { return s.type == SceneLightType::Point || s.type == SceneLightType::Spot; }});
    field("priority", &S::priority, Label{"Priority"}, Advanced{});
};

// 範囲へ丸め、外側の円錐角を内側以上にする。
LightSettings NormalizeLightSettings(LightSettings settings) noexcept;
LightSettings ReadLightSettings(const nlohmann::json& properties);
nlohmann::json WriteLightSettings(const LightSettings& settings);

// 相対の灯の基準照度 [lx]：環境（太陽と空）が水平面を照らす明るさ。場面の描画が毎フレーム更新する。
void SetSceneKeyIlluminance(float lux) noexcept;
float GetSceneKeyIlluminance() noexcept;
// 描画へ渡す光度 [cd]。相対は 1 m 先の照度が「倍率 × 基準照度」になる光度。
float ResolveLuminousIntensity(const LightSettings& settings) noexcept;
// 今の単位での明るさ（物理は lm、相対は倍率）。ゲームの API と表示は単位を問わずこの値を使う。
float GetLightBrightness(const LightSettings& settings) noexcept;
void SetLightBrightness(LightSettings& settings, float brightness) noexcept;
// 単位を切り替え、今の基準照度で同じ明るさになる値へ換算する（画面の明るさが跳ばない）。
LightSettings ConvertLightIntensityUnit(LightSettings settings, LightIntensityUnit unit) noexcept;

} // namespace HIKARI
