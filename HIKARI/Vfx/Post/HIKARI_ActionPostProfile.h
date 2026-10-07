#pragma once
#include "Assets/Post/HIKARI_PostAsset.h"

namespace HIKARI {
struct GameProjectActionPostFrameRequest;
}
namespace HIKARI::POST {
PostParameterBlock ResolveActionPostParameters(const PostEffectDefinition& effect, const PostProfileAsset& profile,
                                               const GameProjectActionPostFrameRequest& request);
}
