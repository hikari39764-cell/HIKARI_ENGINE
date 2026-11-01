#pragma once

#include <string_view>

#include "RenderContracts/Asset/HIKARI_AssetTypeEnum.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Decal/HIKARI_DecalContract.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

// 保存・読み取り・検査器・説明が共有する項目表。並びは検査器の並び（Common が先頭、Advanced は最後の折りたたみ）。
inline constexpr auto kDecalSettingsFields = [](auto&& field) {
    using S = RENDER3D::DECAL::DecalSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});
    field("size", &S::size, Range{0.01, RENDER3D::DECAL::kMaximumDecalExtent}, Label{"Size"}, Unit{"m"},
          Help{"Box size: X width, Y height, Z projection depth along local +Z. Surfaces inside the box receive the "
               "decal; it fades near both ends."},
          Common{});
    field("baseColorTexture", &S::baseColorTexture, Label{"Base Color Texture"},
          Help{"RGB colour and alpha coverage. Reads unmirrored when looking along the projection (texture up = local "
               "+Y)."},
          AssetRef{AssetType::Texture}, Common{});
    field("normalTexture", &S::normalTexture, Label{"Normal Texture"},
          Help{"Tangent-space normal map (same convention as materials)."}, AssetRef{AssetType::Texture}, Common{});
    field("color", &S::color, Range{0.0, 1.0}, Label{"Color"}, Color{}, Common{});
    field("opacity", &S::opacity, Range{0.0, 1.0}, Label{"Opacity"}, Common{});
    field("colorWeight", &S::colorWeight, Range{0.0, 1.0}, Label{"Color Weight"},
          Help{"Zero keeps the surface colour and applies only normal and roughness (wet patches, dents)."});
    field("normalWeight", &S::normalWeight, Range{0.0, 1.0}, Label{"Normal Weight"});
    field("materialWeight", &S::materialWeight, Range{0.0, 1.0}, Label{"Roughness / Metallic Weight"});
    field("roughness", &S::roughness, Range{0.04, 1.0}, Label{"Roughness"},
          Show{[](const S& s) { return s.materialWeight > 0.0f; }});
    field("metallic", &S::metallic, Range{0.0, 1.0}, Label{"Metallic"},
          Show{[](const S& s) { return s.materialWeight > 0.0f; }});
    field("angleFadeDegrees", &S::angleFadeDegrees, Range{0.0, 89.0}, Label{"Angle Fade Start"},
          Help{"Surfaces turned further than this from the projection fade out, reaching zero at 90 degrees."});
    field("sortOrder", &S::sortOrder, Label{"Sort Order"}, Help{"Higher values draw over lower ones."}, Advanced{});
};

// 範囲へ丸める（非有限値は既定値）。
RENDER3D::DECAL::DecalSettings NormalizeDecalSettings(RENDER3D::DECAL::DecalSettings settings) noexcept;
RENDER3D::DECAL::DecalSettings ReadDecalSettings(const nlohmann::json& properties);
nlohmann::json WriteDecalSettings(const RENDER3D::DECAL::DecalSettings& settings);

// 物体の位置を中心にした箱へ、ローカル +Z の向きにテクスチャを投影するデカール。作者値は DecalSettings（シーン文書の
// properties と同じ形）。
class DecalComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "DecalComponent"; }

    void Serialize(nlohmann::json& out) const override { out = WriteDecalSettings(settings_); }
    void Deserialize(const nlohmann::json& in) override { settings_ = ReadDecalSettings(in); }
    // 範囲へ丸めて置き換える（実行時の変更、文書へは保存しない）。
    void SetSettings(const RENDER3D::DECAL::DecalSettings& settings) noexcept {
        settings_ = NormalizeDecalSettings(settings);
    }
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    const RENDER3D::DECAL::DecalSettings& GetSettings() const noexcept { return settings_; }

private:
    RENDER3D::DECAL::DecalSettings settings_{};
};

} // namespace HIKARI
