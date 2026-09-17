#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX::PRESETS {

    struct VfxPresetDescriptor {
        std::string id{};
        std::string displayName{};
        std::string category{};
        std::string summary{};
    };

    struct VfxPresetPackage {
        VfxAssetData asset{};
    };

    // 編集可能な形状・テクスチャ・マテリアル効果・補助粒子の合成。
    // パッケージ固有の不透明なシェーダーを生成しない。
    const std::vector<VfxPresetDescriptor>& GetVfxPresetCatalog();
    const VfxPresetDescriptor* FindVfxPreset(std::string_view id) noexcept;
    VfxAssetData MakeVfxPresetAsset(std::string_view id);
    VfxPresetPackage MakeVfxPresetPackage(std::string_view id);

} // namespace HIKARI::VFX::PRESETS
