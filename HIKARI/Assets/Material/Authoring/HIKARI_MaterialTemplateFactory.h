#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::ASSETS::MATERIAL {

    struct MaterialTemplateInfo {
        MaterialTemplateId id = MaterialTemplateId::StandardPbr;
        std::string_view displayName{};
        std::string_view description{};
    };

    enum class MaterialRampPresetId : uint8_t {
        ToonHard,
        ToonSoft,
        SkinWarm,
        CoolShadow,
    };

    struct MaterialRampPresetInfo {
        MaterialRampPresetId id = MaterialRampPresetId::ToonSoft;
        std::string_view displayName{};
        std::string_view fileStem{};
        std::string_view description{};
    };

    struct CharacterMaterialSetEntry {
        std::string roleId{};
        std::string displayName{};
        std::string recommendedUse{};
        MaterialTemplateId templateId = MaterialTemplateId::StandardPbr;
        MaterialAssetData source{};
    };

    struct DefaultCharacterMaterialSet {
        std::vector<CharacterMaterialSetEntry> entries{};
        std::vector<std::string> diagnostics{};
    };

    const std::array<MaterialTemplateInfo, 8u>&
        GetBuiltInMaterialTemplates() noexcept;

    const std::array<MaterialRampPresetInfo, 4u>&
        GetBuiltInMaterialRampPresets() noexcept;

    MaterialAssetData CreateMaterialRampFromPreset(
        MaterialRampPresetId presetId,
        std::string_view displayName = {});

    bool IsBuiltInMaterialTemplate(
        MaterialTemplateId templateId) noexcept;

    MaterialAssetData CreateMaterialFromTemplate(
        MaterialTemplateId templateId,
        std::string_view displayName = {});

    DefaultCharacterMaterialSet CreateDefaultCharacterMaterialSet(
        std::string_view displayNamePrefix = "Character");

} // namespace HIKARI::ASSETS::MATERIAL
