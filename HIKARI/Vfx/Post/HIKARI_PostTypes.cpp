#include "Vfx/Post/HIKARI_PostTypes.h"

#include <algorithm>

#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"

namespace HIKARI::POST {

const VFX::ParamDesc* FindPostParameter(const PostEffectDefinition& effect, std::string_view key) {
    const auto found = std::find_if(effect.params.begin(), effect.params.end(),
                                    [key](const VFX::ParamDesc& param) { return param.key == key; });
    return found != effect.params.end() ? &*found : nullptr;
}

PostParameterValue GetPostParameterValue(const VFX::ParamDesc& param, const PostProfileAsset* profile) {
    if (profile) {
        if (const auto found = profile->values.find(param.key); found != profile->values.end()) {
            return found->second;
        }
    }
    return {param.defaultValues[0], param.defaultValues[1], param.defaultValues[2], param.defaultValues[3]};
}

void SetPostParameterValue(const VFX::ParamDesc& param, PostProfileAsset& profile, const PostParameterValue& value) {
    profile.values.insert_or_assign(param.key, value);
}

PostParameterBlock ResolvePostParameters(const PostEffectDefinition& effect, const PostProfileAsset* profile) {
    PostParameterBlock block{};
    for (const VFX::ParamDesc& param : effect.params) {
        const PostParameterValue value = GetPostParameterValue(param, profile);
        VFX::WriteParameterValue(param, value.data(), block.data(), block.size());
    }
    return block;
}

std::optional<float> ReadPostParameter(const PostEffectDefinition& effect, std::span<const DirectX::XMFLOAT4> values,
                                       std::string_view key) {
    const VFX::ParamDesc* param = FindPostParameter(effect, key);
    if (!param || param->ref.slot >= values.size() || param->ref.channel >= 4u) {
        return std::nullopt;
    }
    const float* channels = &values[param->ref.slot].x;
    return channels[param->ref.channel];
}

} // namespace HIKARI::POST
