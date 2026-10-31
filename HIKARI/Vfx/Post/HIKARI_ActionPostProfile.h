#pragma once
#include "Assets/Post/HIKARI_PostAsset.h"

namespace HIKARI {
struct GameProjectActionPostFrameRequest;
}
namespace HIKARI::POST {
// 行動の一時効果は HDR の単一時点で実行する。全体効果の前後 pass はこの入口へ混ぜない。
bool IsActionPostEffectCompatible(const PostEffectDefinition& effect) noexcept;
PostParameterBlock ResolveActionPostParameters(const PostEffectDefinition& effect, const PostProfileAsset& profile,
                                               const GameProjectActionPostFrameRequest& request);
} // namespace HIKARI::POST
