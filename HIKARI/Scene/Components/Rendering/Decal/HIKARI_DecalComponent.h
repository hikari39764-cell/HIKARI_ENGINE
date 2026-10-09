#pragma once

#include <string_view>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Decal/HIKARI_DecalContract.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

inline constexpr auto kDecalSettingsFields = [](auto&& field) {
    using S = RENDER3D::DECAL::DecalSettings;
    using JSONFIELDS::Range;
    field("enabled", &S::enabled);
    field("size", &S::size, Range{0.01, RENDER3D::DECAL::kMaximumDecalExtent});
    field("baseColorTexture", &S::baseColorTexture);
    field("normalTexture", &S::normalTexture);
    field("color", &S::color, Range{0.0, 1.0});
    field("opacity", &S::opacity, Range{0.0, 1.0});
    field("colorWeight", &S::colorWeight, Range{0.0, 1.0});
    field("normalWeight", &S::normalWeight, Range{0.0, 1.0});
    field("materialWeight", &S::materialWeight, Range{0.0, 1.0});
    field("roughness", &S::roughness, Range{0.04, 1.0});
    field("metallic", &S::metallic, Range{0.0, 1.0});
    field("angleFadeDegrees", &S::angleFadeDegrees, Range{0.0, 89.0});
    field("sortOrder", &S::sortOrder);
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
