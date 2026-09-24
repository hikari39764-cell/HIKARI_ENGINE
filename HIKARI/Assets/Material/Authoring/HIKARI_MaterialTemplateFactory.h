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

const std::array<MaterialTemplateInfo, 10u>& GetBuiltInMaterialTemplates() noexcept;

const std::array<MaterialRampPresetInfo, 4u>& GetBuiltInMaterialRampPresets() noexcept;

MaterialAssetData CreateMaterialRampFromPreset(MaterialRampPresetId presetId, std::string_view displayName = {});

// Creates a valid one-input/one-output Function that can immediately be
// discovered by the node palette and then expanded into a richer graph.
MaterialAssetData CreatePassThroughMaterialFunction(std::string_view displayName = "New Material Function");

bool IsBuiltInMaterialTemplate(MaterialTemplateId templateId) noexcept;

MaterialAssetData CreateMaterialFromTemplate(MaterialTemplateId templateId, std::string_view displayName = {});

// Seeds the standard PBR inputs before the selected template augments the
// graph. This keeps authored texture sample/normal edges reachable for
// stylized templates instead of trying to retrofit resources afterwards.
MaterialAssetData CreateMaterialFromTemplateWithPbr(MaterialTemplateId templateId, const PbrMaterialAssetData& pbr,
                                                    std::string_view displayName = {});

} // namespace HIKARI::ASSETS::MATERIAL
