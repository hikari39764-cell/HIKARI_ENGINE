#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetArtifactManifest.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Vfx/Post/HIKARI_PostTypes.h"

// 全画面後処理の資産。効果（.hposteffect）は pass とパラメーターの定義、
// プロファイル（.hpostprofile）は効果の参照とパラメーター値を持ち、シーンの環境はプロファイルを参照する。
// 描画が読む型（効果・プロファイル・解決済みの層）は Vfx/Post/HIKARI_PostTypes.h にあり、ここは読み書きと依存の収集。
namespace HIKARI::POST {

inline constexpr uint32_t kPostEffectFormatVersion = 1u;
inline constexpr uint32_t kPostProfileFormatVersion = 1u;

// 効果がシーン色（t0）のほかに読む画面の入力。深度は t1（R32_FLOAT、未射影の深度）、特性マスクは t2。
enum class PostEffectInput : uint8_t {
    SceneDepth,
    SurfaceFeatureMask,
};
inline constexpr TEXT::EnumName<PostEffectInput> kPostEffectInputNames[] = {
    {PostEffectInput::SceneDepth, "SceneDepth"},
    {PostEffectInput::SurfaceFeatureMask, "SurfaceFeatureMask"},
};

bool ReadPostEffectDefinition(const nlohmann::json& root, PostEffectDefinition& out, std::string& error);
bool LoadPostEffectDefinition(const std::filesystem::path& path, PostEffectDefinition& out, std::string& error);

bool ReadPostProfileAsset(const nlohmann::json& root, PostProfileAsset& out, std::string& error);
nlohmann::json WritePostProfileAsset(const PostProfileAsset& profile);
bool LoadPostProfileAsset(const std::filesystem::path& path, PostProfileAsset& out, std::string& error);
bool SavePostProfileAsset(const std::filesystem::path& path, const PostProfileAsset& profile, std::string& error);
// プロファイルが参照するプロジェクトの効果資産。エンジン内蔵の効果は依存に含めない。
std::vector<AssetDependencyDesc> CollectPostProfileDependencies(const PostProfileAsset& profile);
std::vector<AssetDependencyDesc> CollectPostEffectDependencies(const PostEffectDefinition& effect);

// プロファイルの値が効果のパラメーターに対応しているか。
bool ValidatePostProfileValues(const PostProfileAsset& profile, const PostEffectDefinition& effect, std::string& error);

// エンジンが初期内容として持つ効果と、その既定プロファイル。
struct BuiltinPostContent {
    std::string_view effectId;
    std::string_view profileId;
    std::string_view displayName;
    std::string_view fileStem;
};
inline constexpr std::string_view kBuiltinPostEffectPrefix = "engine.post.effect.";
inline constexpr std::string_view kBuiltinPostProfilePrefix = "engine.post.profile.";
std::span<const BuiltinPostContent> GetBuiltinPostContent() noexcept;
std::filesystem::path ResolveBuiltinPostEffectPath(std::string_view effectId);
std::filesystem::path ResolveBuiltinPostProfilePath(std::string_view profileId);

} // namespace HIKARI::POST
