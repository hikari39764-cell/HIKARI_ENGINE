#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Authoring/HIKARI_FieldDraw.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeContract.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

inline constexpr TEXT::EnumName<ReflectionProbeInfluenceShape> kReflectionProbeInfluenceShapeNames[] = {
    {ReflectionProbeInfluenceShape::Sphere, "Sphere"},
    {ReflectionProbeInfluenceShape::Box, "Box"},
};
inline constexpr TEXT::EnumName<ReflectionProbeProjectionShape> kReflectionProbeProjectionShapeNames[] = {
    {ReflectionProbeProjectionShape::Infinite, "Infinite"},
    {ReflectionProbeProjectionShape::Box, "Box"},
};

// 作者値（シーン文書の properties と同じ形）。項目表（kReflectionProbeFields）が保存・読み取り・検査器・説明を決める。
// ReflectionProbeSettings（描画側の契約。位置は絶対座標）とは別で、こちらは物体からの相対で持つ。
struct ReflectionProbeComponentSettings {
    bool enabled = true;
    std::string sourceCubemapAsset{};
    float radius = 8.0f;
    float intensity = 1.0f;
    ReflectionProbeInfluenceShape influenceShape = ReflectionProbeInfluenceShape::Sphere;
    MATH::Vec3 influenceBoxOffset{0.0f, 0.0f, 0.0f};
    MATH::Vec3 influenceBoxSize{8.0f, 4.0f, 8.0f};
    // 既定は影響範囲へ連動した投影（半径 8 の球 → 一辺 16 の箱）。NormalizeReflectionProbeSettings の結果と同じにしておく。
    ReflectionProbeProjectionShape projectionShape = ReflectionProbeProjectionShape::Box;
    MATH::Vec3 projectionBoxOffset{0.0f, 0.0f, 0.0f};
    MATH::Vec3 projectionBoxSize{16.0f, 16.0f, 16.0f};
    bool projectionFollowsInfluence = true;
    float blendDistance = 1.0f;
    int priority = 0;
    uint32_t captureResolution = 128;
    std::string bakeSet{"Default"};
    std::string scenario{"Default"};
};

inline constexpr auto kReflectionProbeFields = [](auto&& field) {
    using S = ReflectionProbeComponentSettings;
    using namespace JSONFIELDS;
    constexpr auto box = [](const S& s) { return s.influenceShape == ReflectionProbeInfluenceShape::Box; };
    constexpr auto unlinked = [](const S& s) { return !s.projectionFollowsInfluence; };
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});
    field("sourceCubemapAsset", &S::sourceCubemapAsset, Label{"Source Cubemap Asset"}, Common{});
    field("radius", &S::radius, Range{0.05, 100000.0}, Label{"Sphere / Capture Radius"}, Common{});
    field("intensity", &S::intensity, Range{0.0, 8.0}, Label{"Intensity"}, Common{});
    field(
        "captureResolution", &S::captureResolution, Label{"Capture Quality"},
        Help{"Higher quality uses a full roughness mip chain and more samples."}, Common{},
        Draw{FIELD_DRAW::NumberChoice3{{64u, 128u, 256u},
                                       {AuthoringChoice{"64", "64 - Preview"}, AuthoringChoice{"128", "128 - Standard"},
                                        AuthoringChoice{"256", "256 - High"}}}});

    constexpr const char* kInfluence = "Influence";
    field("influenceShape", &S::influenceShape, kReflectionProbeInfluenceShapeNames, Label{"Influence Shape"},
          Group{kInfluence});
    field("influenceBoxOffset", &S::influenceBoxOffset, Label{"Influence Offset"}, Show{box}, Group{kInfluence});
    field("influenceBoxSize", &S::influenceBoxSize, Range{0.05, 100000.0}, Label{"Influence Size"}, Show{box},
          Group{kInfluence});
    field("blendDistance", &S::blendDistance, Range{0.0, 100000.0}, Label{"Blend Distance"}, Group{kInfluence});
    field("priority", &S::priority, Label{"Priority"}, Group{kInfluence});

    constexpr const char* kProjection = "Projection";
    field("projectionFollowsInfluence", &S::projectionFollowsInfluence, Label{"Link Projection To Influence"},
          Help{"The projection box follows the influence bounds (a sphere is projected as its bounding box)."},
          Group{kProjection});
    field("projectionShape", &S::projectionShape, kReflectionProbeProjectionShapeNames, Label{"Projection Shape"},
          Show{unlinked}, Group{kProjection});
    field("projectionBoxOffset", &S::projectionBoxOffset, Label{"Projection Offset"}, Show{unlinked},
          Group{kProjection});
    field("projectionBoxSize", &S::projectionBoxSize, Range{0.05, 100000.0}, Label{"Projection Size"}, Show{unlinked},
          Group{kProjection});

    constexpr const char* kRouting = "Bake Routing";
    field("bakeSet", &S::bakeSet, Label{"Bake Set"}, Group{kRouting});
    field("scenario", &S::scenario, Label{"Scenario"}, Group{kRouting});
};

// 投影の箱を影響範囲へ合わせる（球の影響範囲は外接する箱で投影する）。
void AlignReflectionProbeProjection(ReflectionProbeComponentSettings& settings) noexcept;
// 範囲へ丸め、撮影解像度を 64 / 128 / 256 のどれかにし、空の振り分け名を既定へ戻す。連動が有効なら投影の箱を影響範囲へ
// 合わせる。
ReflectionProbeComponentSettings NormalizeReflectionProbeSettings(ReflectionProbeComponentSettings settings);

class ReflectionProbeComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "ReflectionProbeComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return settings_.enabled; }
    float GetRadius() const noexcept { return settings_.radius; }
    bool SetRadius(float radius) noexcept;
    float GetIntensity() const noexcept { return settings_.intensity; }
    ReflectionProbeInfluenceShape GetInfluenceShape() const noexcept { return settings_.influenceShape; }
    const MATH::Vec3& GetInfluenceBoxOffset() const noexcept { return settings_.influenceBoxOffset; }
    const MATH::Vec3& GetInfluenceBoxSize() const noexcept { return settings_.influenceBoxSize; }
    bool SetInfluenceBoxSize(const MATH::Vec3& size) noexcept;
    bool GetProjectionFollowsInfluence() const noexcept { return settings_.projectionFollowsInfluence; }
    ReflectionProbeProjectionShape GetProjectionShape() const noexcept { return settings_.projectionShape; }
    const MATH::Vec3& GetProjectionBoxOffset() const noexcept { return settings_.projectionBoxOffset; }
    const MATH::Vec3& GetProjectionBoxSize() const noexcept { return settings_.projectionBoxSize; }
    float GetBlendDistance() const noexcept { return settings_.blendDistance; }
    int GetPriority() const noexcept { return settings_.priority; }
    uint32_t GetCaptureResolution() const noexcept { return settings_.captureResolution; }
    const std::string& GetBakeSet() const noexcept { return settings_.bakeSet; }
    const std::string& GetScenario() const noexcept { return settings_.scenario; }

private:
    ReflectionProbeComponentSettings settings_{};
};

} // namespace HIKARI
