#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <DirectXMath.h>
#include <json.hpp>

#include "Assets/HIKARI_AssetArtifactManifest.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

// 全画面後処理の資産。効果（.hposteffect）は pass とパラメーターの定義、
// プロファイル（.hpostprofile）は効果の参照とパラメーター値を持ち、シーンの環境はプロファイルを参照する。
namespace HIKARI::POST {

inline constexpr uint32_t kPostEffectFormatVersion = 1u;
inline constexpr uint32_t kPostProfileFormatVersion = 1u;
inline constexpr size_t kPostParameterSlotCount = 16u;
using PostParameterBlock = std::array<DirectX::XMFLOAT4, kPostParameterSlotCount>;
using PostParameterValue = std::array<float, 4>;

// 画面全体を曲げる効果が、どのパラメーターで量を決めるか。
struct PostScreenWarpBinding {
    std::string strengthParam;
    std::string curvatureParam;
};

struct PostEffectPass {
    std::string name;
    std::string programId;
    std::string entryId = "Main";
};

// 効果の ID は読み込んだ場所が決める（エンジン内蔵は "engine.post.effect.*"、プロジェクト資産は GUID）。
struct PostEffectDefinition {
    std::string name;
    std::vector<PostEffectPass> passes;
    std::vector<VFX::ParamDesc> params;
    std::optional<PostScreenWarpBinding> screenWarp;
};

struct PostProfileAsset {
    std::string name = "Post Profile";
    std::string effectId;
    // パラメーターキー → 値。無いキーは効果の既定値を使う。
    std::map<std::string, PostParameterValue> values;
};

bool ReadPostEffectDefinition(const nlohmann::json& root, PostEffectDefinition& out, std::string& error);
bool LoadPostEffectDefinition(const std::filesystem::path& path, PostEffectDefinition& out, std::string& error);

bool ReadPostProfileAsset(const nlohmann::json& root, PostProfileAsset& out, std::string& error);
nlohmann::json WritePostProfileAsset(const PostProfileAsset& profile);
bool LoadPostProfileAsset(const std::filesystem::path& path, PostProfileAsset& out, std::string& error);
bool SavePostProfileAsset(const std::filesystem::path& path, const PostProfileAsset& profile, std::string& error);
// プロファイルが参照するプロジェクトの効果資産。エンジン内蔵の効果は依存に含めない。
std::vector<AssetDependencyDesc> CollectPostProfileDependencies(const PostProfileAsset& profile);

// プロファイルの値が効果のパラメーターに対応しているか。
bool ValidatePostProfileValues(const PostProfileAsset& profile, const PostEffectDefinition& effect, std::string& error);
// 効果の既定値にプロファイルの値を重ねたパラメーターブロック。profile が無ければ既定値のまま。
PostParameterBlock ResolvePostParameters(const PostEffectDefinition& effect, const PostProfileAsset* profile);
// パラメーターキーの現在値（先頭チャンネル）。見つからなければ nullopt。
std::optional<float> ReadPostParameter(const PostEffectDefinition& effect, std::span<const DirectX::XMFLOAT4> values,
                                       std::string_view key);
PostParameterValue GetPostParameterValue(const VFX::ParamDesc& param, const PostProfileAsset* profile);
void SetPostParameterValue(const VFX::ParamDesc& param, PostProfileAsset& profile, const PostParameterValue& value);

// エンジンが初期内容として持つ効果と、その既定プロファイル。
struct BuiltinPostContent {
    std::string_view effectId;
    std::string_view profileId;
    std::string_view displayName;
    std::string_view fileStem;
};
inline constexpr std::string_view kBuiltinPostEffectPrefix = "engine.post.effect.";
inline constexpr std::string_view kBuiltinPostProfilePrefix = "engine.post.profile.";
// 後処理を有効にした時の既定プロファイル。
inline constexpr std::string_view kDefaultPostProfileId = "engine.post.profile.ani";
std::span<const BuiltinPostContent> GetBuiltinPostContent() noexcept;
std::filesystem::path ResolveBuiltinPostEffectPath(std::string_view effectId);
std::filesystem::path ResolveBuiltinPostProfilePath(std::string_view profileId);

} // namespace HIKARI::POST
