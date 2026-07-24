#pragma once

#include <string>
#include <array>
#include <vector>
#include <DirectXMath.h>

#include "Vfx/Common/HIKARI_FxTypes.h"
#include "Vfx/Post/HIKARI_PostCommon.h"

namespace HIKARI {

class PostProfile {
public:
    std::string id;
    std::string displayName;
    VFX::FxDomain domain = VFX::FxDomain::GlobalPost;
    std::vector<VFX::PassDescriptor> passes;
    std::vector<VFX::ParamDesc> params;
    std::array<DirectX::XMFLOAT4, 16> values{};

    static bool LoadById(const std::string& profileId, PostProfile& out);
    bool LoadFromJson(const std::string& path);
    void ResetValuesFromDefaults();
    void CopyValuesTo(DirectX::XMFLOAT4 (&dst)[16]) const;
    void ApplyToCommonParams(POST::CommonParams& out) const;
};

} // namespace HIKARI
