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

// 編集可能な形状・テクスチャ・マテリアル効果・補助粒子の合成。
// パッケージ固有の不透明なシェーダーを生成しない。
const std::vector<VfxPresetDescriptor>& GetVfxPresetCatalog();
const VfxPresetDescriptor* FindVfxPreset(std::string_view id) noexcept;
VfxAssetData MakeVfxPresetAsset(std::string_view id);
// 種類 kind の新しい層の既定を既定の層（MakeDefaultVfxAssetData の層）へ当てる。編集器の「層を足す」と预设が共有する。
// id と親は呼ぶ側が決める。
void ApplyLayerKindDefaults(VfxEmitterData& emitter, VfxLayerKind kind);

} // namespace HIKARI::VFX::PRESETS
