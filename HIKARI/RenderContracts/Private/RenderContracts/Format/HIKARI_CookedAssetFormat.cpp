#include "RenderContracts/Format/HIKARI_CookedAssetFormat.h"

#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::ASSETS::SEMANTICS {

namespace {

constexpr TEXT::EnumName<CookedAssetFormat> kCookedAssetFormatNames[] = {
    {CookedAssetFormat::DDS, "DDS"},
    {CookedAssetFormat::HTEX, "HTEX"},
    {CookedAssetFormat::HMODEL, "HMODEL"},
    {CookedAssetFormat::HMESH, "HMESH"},
    {CookedAssetFormat::HCMESH, "HCMESH"},
    {CookedAssetFormat::HCOLLISION, "HCOLLISION"},
    {CookedAssetFormat::HMAT, "HMAT"},
    {CookedAssetFormat::HSKY, "HSKY"},
    {CookedAssetFormat::HIBL, "HIBL"},
    {CookedAssetFormat::HVFX, "HVFX"},
    {CookedAssetFormat::HSHADERC, "HSHADERC"},
    {CookedAssetFormat::HPAK, "HPAK"},
    {CookedAssetFormat::HPREFABC, "HPREFABC"},
    {CookedAssetFormat::HSPRITEC, "HSPRITEC"},
    {CookedAssetFormat::HSPRITEMATERIALC, "HSPRITEMATERIALC"},
};

constexpr TEXT::EnumName<CookedAssetFormat> kCookedAssetFormatExtensions[] = {
    {CookedAssetFormat::DDS, ".dds"},
    {CookedAssetFormat::HTEX, ".htex"},
    {CookedAssetFormat::HMODEL, ".hmodel"},
    {CookedAssetFormat::HMESH, ".hmesh"},
    {CookedAssetFormat::HCMESH, ".hcmesh"},
    {CookedAssetFormat::HCOLLISION, ".hcollision"},
    {CookedAssetFormat::HMAT, ".hmat"},
    {CookedAssetFormat::HSKY, ".hsky"},
    {CookedAssetFormat::HIBL, ".hibl"},
    {CookedAssetFormat::HVFX, ".hvfxc"},
    {CookedAssetFormat::HSHADERC, ".hshaderc"},
    {CookedAssetFormat::HPAK, ".hpak"},
    {CookedAssetFormat::HPREFABC, ".hprefabc"},
    {CookedAssetFormat::HSPRITEC, ".hspritec"},
    {CookedAssetFormat::HSPRITEMATERIALC, ".hspritematerialc"},
};

} // namespace

std::string_view ToString(CookedAssetFormat format) noexcept {
    return TEXT::EnumToName(kCookedAssetFormatNames, format, "Unknown");
}

CookedAssetFormat ParseCookedAssetFormat(std::string_view text) noexcept {
    return TEXT::ParseEnum(kCookedAssetFormatNames, text, CookedAssetFormat::Unknown);
}

CookedAssetFormat ClassifyCookedAssetFormat(const std::filesystem::path& path) {
    const std::string extension = TEXT::ToLowerAsciiCopy(path.extension().string());
    return TEXT::ParseEnum(kCookedAssetFormatExtensions, extension, CookedAssetFormat::Unknown);
}

} // namespace HIKARI::ASSETS::SEMANTICS
