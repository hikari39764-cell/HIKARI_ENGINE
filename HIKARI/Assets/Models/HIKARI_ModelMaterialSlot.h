#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"

namespace HIKARI::ASSETS::MODELS {

// Values are serialized in HMODEL. Append new semantics; do not reorder.
enum class MaterialSlotImporterSemantic : uint8_t {
    LegacyHmodel = 0u,
    Gltf = 1u,
    Assimp = 2u,
    Obj = 3u,
    Procedural = 4u,
};

MaterialSlotId MakeMaterialSlotId(MaterialSlotImporterSemantic importerSemantic,
                                  std::string_view sourceMaterialIdentity, uint32_t duplicateOrdinal);

class MaterialSlotIdBuilder {
public:
    explicit MaterialSlotIdBuilder(MaterialSlotImporterSemantic importerSemantic) noexcept;

    MaterialSlotId Next(std::string_view sourceMaterialIdentity);

private:
    MaterialSlotImporterSemantic importerSemantic_;
    std::unordered_map<std::string, uint32_t> duplicateCounts_{};
};

bool HaveValidUniqueMaterialSlotIds(const std::vector<MaterialAsset>& materials);

// 安定 slot ID から現在の material index を引く。見つからなければ nullopt。
std::optional<uint32_t> FindMaterialSlotIndex(const std::vector<MaterialAsset>& materials,
                                              MaterialSlotId slotId) noexcept;

std::string MaterialSlotIdToString(MaterialSlotId slotId);
std::optional<MaterialSlotId> ParseMaterialSlotId(std::string_view text) noexcept;

} // namespace HIKARI::ASSETS::MODELS
