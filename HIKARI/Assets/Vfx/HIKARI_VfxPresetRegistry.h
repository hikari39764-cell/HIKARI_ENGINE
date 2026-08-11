#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX::PRESETS {

    struct VfxPresetDescriptor {
        std::string id{};
        std::string displayName{};
        std::string category{};
        std::string summary{};
    };

    struct VfxPresetMaterialRecipe {
        std::string emitterId{};
        std::string displayName{};
        AssetGuid textureAssetId{};

        bool operator==(
            const VfxPresetMaterialRecipe&) const noexcept = default;
    };

    struct VfxPresetPackage {
        VfxAssetData asset{};
        std::vector<VfxPresetMaterialRecipe> privateMaterials{};
    };

    // Effekseer-aligned starter presets. Each factory builds a complete,
    // ready-to-edit VfxAssetData plus VFX-private material recipes. Recipes
    // reference reusable EngineContent textures and are materialized only when
    // the transient document is first saved.
    const std::vector<VfxPresetDescriptor>& GetVfxPresetCatalog();
    const VfxPresetDescriptor* FindVfxPreset(std::string_view id) noexcept;
    VfxAssetData MakeVfxPresetAsset(std::string_view id);
    VfxPresetPackage MakeVfxPresetPackage(std::string_view id);

} // namespace HIKARI::VFX::PRESETS
