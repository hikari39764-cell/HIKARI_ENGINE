#pragma once

#include <span>
#include <string_view>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX {

    // 形状＋パラメータ＋変形の組み合わせに名前を付けたもの。新しい図元を
    // 増やすのではなく、既存の形状と修飾子をプリセット化して再利用する。
    struct VfxGeometryPresetDescriptor {
        std::string_view id{};
        std::string_view displayName{};
        std::string_view category{};
        // type と全パラメータ。id は適用時に保持される。
        VfxRenderShapeModuleData shape{};
    };

    std::span<const VfxGeometryPresetDescriptor>
        GetVfxGeometryPresetCatalog() noexcept;
    const VfxGeometryPresetDescriptor* FindVfxGeometryPreset(
        std::string_view id) noexcept;
    // プリセットの形状設定を書き込む。既存の id は保持する。
    void ApplyVfxGeometryPreset(
        const VfxGeometryPresetDescriptor& preset,
        VfxRenderShapeModuleData& outShape) noexcept;

} // namespace HIKARI::VFX
