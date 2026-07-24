#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI {

struct MaterialFxProfileCacheStats {
    size_t hitCount = 0;
    size_t missCount = 0;
    size_t failCount = 0;
};

enum class MaterialFxRenderPhase {
    Opaque = 0,
    DepthAware,
};

class MaterialFxProfile {
public:
    std::string id;
    std::string displayName;
    std::string shaderProfileId;
    std::string vertexShaderId;
    std::string pixelShaderId;
    uint32_t featureBits = 0;
    bool depthTest = true;
    bool depthWrite = true;
    bool doubleSided = false;
    VFX::CompositeMode composite = VFX::CompositeMode::Alpha;
    MaterialFxRenderPhase renderPhase = MaterialFxRenderPhase::Opaque;
    std::vector<VFX::ParamDesc> params;
    std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount> values{};

    static bool LoadById(const std::string& profileId, MaterialFxProfile& out);
    static void ClearCache();
    static MaterialFxProfileCacheStats GetCacheStats();
    bool LoadFromJson(const std::string& path);
    void CopyValuesTo(DirectX::XMFLOAT4 (&dst)[VFX::kMaterialFxUserCount]) const;
};

} // namespace HIKARI
