#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetArtifactManifest.h"
#include "RenderContracts/RenderStyle/HIKARI_RenderStyleShading.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"

// 描画の様式（.hrenderstyle）。画面全体の見た目の作り方（標準の材質の光の受け方、後処理の重ね方など）を一つに
// まとめた資産で、シーンの環境の参照が選ぶ。空・光・天気を持つ環境プロファイルとは独立に切り替える
// （写実の空のまま漫画風にする、など）。
namespace HIKARI::RENDERSTYLE {

inline constexpr uint32_t kRenderStyleFormatVersion = 1u;
inline constexpr size_t kMaxRenderStylePostLayers = 8u;

// 後処理の層。並びの順に重ねる。
struct RenderStylePostLayer {
    // 内蔵 ID（engine.post.profile.*）か .hpostprofile の GUID。
    std::string profileId;
    bool enabled = true;

    bool operator==(const RenderStylePostLayer&) const = default;
};

inline constexpr TEXT::EnumName<RenderStyleShadingModel> kRenderStyleShadingModelNames[] = {
    {RenderStyleShadingModel::Physical, "Physical"},
    {RenderStyleShadingModel::Cel, "Cel"},
};

inline constexpr auto kRenderStyleShadingFields = [](auto&& field) {
    using JSONFIELDS::Range;
    using S = RenderStyleShading;
    field("model", &S::model, kRenderStyleShadingModelNames);
    field("bands", &S::bands, Range{1.0, 4.0});
    field("threshold", &S::threshold, Range{0.0, 0.95});
    field("softness", &S::softness, Range{0.0, 0.5});
    field("shadowTint", &S::shadowTint, Range{0.0, 2.0});
    field("specular", &S::specular, Range{0.0, 2.0});
    field("rim", &S::rim, Range{0.0, 2.0});
};

struct RenderStyleAsset {
    std::string name = "Render Style";
    RenderStyleShading shading{};
    std::vector<RenderStylePostLayer> post;
};

bool ReadRenderStyleAsset(const nlohmann::json& root, RenderStyleAsset& out, std::string& error);
nlohmann::json WriteRenderStyleAsset(const RenderStyleAsset& style);
bool LoadRenderStyleAsset(const std::filesystem::path& path, RenderStyleAsset& out, std::string& error);
bool SaveRenderStyleAsset(const std::filesystem::path& path, const RenderStyleAsset& style, std::string& error);
// 様式が参照するプロジェクトの後処理プロファイル。エンジン内蔵のプロファイルは依存に含めない。
std::vector<AssetDependencyDesc> CollectRenderStyleDependencies(const RenderStyleAsset& style);

// エンジンが初期内容として持つ様式。
struct BuiltinRenderStyleContent {
    std::string_view id;
    std::string_view displayName;
    std::string_view fileStem;
};
inline constexpr std::string_view kBuiltinRenderStylePrefix = "engine.style.";
inline constexpr std::string_view kDefaultRenderStyleId = "engine.style.realistic";
std::span<const BuiltinRenderStyleContent> GetBuiltinRenderStyles() noexcept;
std::filesystem::path ResolveBuiltinRenderStylePath(std::string_view id);

} // namespace HIKARI::RENDERSTYLE
