#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <DirectXMath.h>

#include "Core/HIKARI_AssetGuid.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

// 全画面後処理が描画で読む型（効果の定義、プロファイルの値、解決済みの層）。
// 資産ファイルの読み書きと依存の収集は Assets/Post/HIKARI_PostAsset.h。
namespace HIKARI::POST {

inline constexpr size_t kPostParameterSlotCount = 16u;
using PostParameterBlock = std::array<DirectX::XMFLOAT4, kPostParameterSlotCount>;
using PostParameterValue = std::array<float, 4>;

// 画面全体を曲げる効果が、どのパラメーターで量を決めるか。
struct PostScreenWarpBinding {
    std::string strengthParam;
    std::string curvatureParam;
};

// 効果を通す段階。PreTemporal は jitter のある HDR、Scene は時系列解決後の HDR、Display は色調写像と FXAA の後の表示の色
// （ガンマ符号化済みの 0..1）。調色板・網点・紙の白のように表示の色で決める効果は Display で書く。
enum class PostEffectPhase : uint8_t {
    PreTemporal,
    Scene,
    Display,
};
inline constexpr TEXT::EnumName<PostEffectPhase> kPostEffectPhaseNames[] = {
    {PostEffectPhase::PreTemporal, "PreTemporal"},
    {PostEffectPhase::Scene, "Scene"},
    {PostEffectPhase::Display, "Display"},
};

struct PostEffectPass {
    std::string name;
    std::string programId;
    std::string entryId = "Main";
    AssetGuid shaderAssetGuid{};
    // 同じパラメーターで、幾何の線は解決前、網点などは表示段階に分けられる。
    PostEffectPhase phase = PostEffectPhase::Scene;
};

// 効果の ID は読み込んだ場所が決める（エンジン内蔵は "engine.post.effect.*"、プロジェクト資産は GUID）。
struct PostEffectDefinition {
    std::string name;
    std::vector<PostEffectPass> passes;
    std::vector<VFX::ParamDesc> params;
    std::optional<PostScreenWarpBinding> screenWarp;
    bool requiresSceneDepth = false;
    bool requiresSurfaceFeatureMask = false;
};

struct PostProfileAsset {
    std::string name = "Post Profile";
    std::string effectId;
    // パラメーターキー → 値。無いキーは効果の既定値を使う。
    std::map<std::string, PostParameterValue> values;
};

const VFX::ParamDesc* FindPostParameter(const PostEffectDefinition& effect, std::string_view key);

// 効果の既定値にプロファイルの値を重ねたパラメーターブロック。profile が無ければ既定値のまま。
PostParameterBlock ResolvePostParameters(const PostEffectDefinition& effect, const PostProfileAsset* profile);

// 解決済みの全体後処理の層（効果の定義と、プロファイルを重ねたパラメーター）。描画の様式の並びの順に重ねる。
struct ResolvedPostLayer {
    std::string effectId;
    std::shared_ptr<const PostEffectDefinition> effect;
    PostParameterBlock values{};
};
// パラメーターキーの現在値（先頭チャンネル）。見つからなければ nullopt。
std::optional<float> ReadPostParameter(const PostEffectDefinition& effect, std::span<const DirectX::XMFLOAT4> values,
                                       std::string_view key);
PostParameterValue GetPostParameterValue(const VFX::ParamDesc& param, const PostProfileAsset* profile);
void SetPostParameterValue(const VFX::ParamDesc& param, PostProfileAsset& profile, const PostParameterValue& value);

} // namespace HIKARI::POST
