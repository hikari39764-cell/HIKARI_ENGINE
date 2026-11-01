#pragma once

#include <filesystem>
#include <string_view>

namespace HIKARI {

enum class CookedAssetFormat {
    Unknown,
    DDS,
    HTEX,
    HMODEL,
    HMESH,
    HCMESH,
    HCOLLISION,
    HMAT,
    HSKY,
    HIBL,
    HVFX,
    HSHADERC,
    HPAK,
    HPREFABC,
    HSPRITEC,
    HSPRITEMATERIALC,
};

namespace ASSETS::SEMANTICS {

std::string_view ToString(CookedAssetFormat format) noexcept;
CookedAssetFormat ParseCookedAssetFormat(std::string_view text) noexcept;
// 拡張子から焼き込み形式を判定する。
CookedAssetFormat ClassifyCookedAssetFormat(const std::filesystem::path& path);

} // namespace ASSETS::SEMANTICS

} // namespace HIKARI
