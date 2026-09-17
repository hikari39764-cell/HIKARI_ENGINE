#pragma once
#include <span>
#include <string>
#include <vector>
#include "Render3D/HIKARI_Math3D.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {
    struct MaterialAssetData;
    enum class MaterialEffectKind : uint8_t {
        UvFlow, UvDistortion, Gradient, Fade, Wipe, Dissolve, Emission, Fresnel
    };
    struct MaterialEffectStep {
        std::string id;
        MaterialEffectKind kind = MaterialEffectKind::UvFlow;
        bool enabled = true;
        MATH::Vec4 parameters{};
        MATH::Vec4 colorA{1, 1, 1, 1};
        MATH::Vec4 colorB{1, 1, 1, 1};
    };
    std::string_view MaterialEffectName(MaterialEffectKind) noexcept;
    bool ParseMaterialEffectKind(std::string_view, MaterialEffectKind&) noexcept;
    MaterialEffectStep MakeMaterialEffect(MaterialEffectKind, std::string id);
    // 全て既存の材質ノードへ展開。VFX 専用の GPU インタープリターは持たない。
    MaterialAssetData BuildMaterialEffectStack(const AssetGuid& texture,
        std::span<const MaterialEffectStep> effects, bool lit = false,
        uint32_t atlasColumns = 1, uint32_t atlasRows = 1,
        std::span<const std::string> animatedParameters = {});
}
