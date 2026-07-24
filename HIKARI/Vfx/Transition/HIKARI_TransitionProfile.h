#pragma once

#include <array>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Vfx/Common/HIKARI_FxTypes.h"
#include "Vfx/Post/HIKARI_PostCommon.h"

namespace HIKARI {

class TransitionProfile {
public:
    std::string id;
    std::string displayName;
    std::string shaderId;

    float outDuration = 0.25f;
    float inDuration = 0.25f;

    std::vector<VFX::ParamDesc> params;
    std::array<DirectX::XMFLOAT4, 16> values{};

    bool LoadFromJson(const std::string& path);
    static bool LoadById(const std::string& profileId, TransitionProfile& outProfile);

    void ResetValuesFromDefaults();
    void ApplyToCommonParams(POST::CommonParams& out) const;
};

} // namespace HIKARI
