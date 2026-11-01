#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Model/HIKARI_ModelMaterialSlot.h"

namespace HIKARI::ASSETS::MODELS {

inline constexpr uint32_t kModelMaterialBindingAssetVersion = 1u;

struct ModelMaterialBindingEntry {
    MaterialSlotId slotId{};
    AssetGuid materialAssetGuid{};
};

struct ModelMaterialBindingAssetData {
    uint32_t version = kModelMaterialBindingAssetVersion;
    std::string displayName = "Model Material Bindings";
    AssetGuid modelAssetGuid{};
    std::vector<ModelMaterialBindingEntry> bindings{};
};

} // namespace HIKARI::ASSETS::MODELS
