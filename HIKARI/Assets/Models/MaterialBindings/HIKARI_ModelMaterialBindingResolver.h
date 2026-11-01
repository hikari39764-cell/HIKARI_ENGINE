#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"
#include "RenderContracts/Model/HIKARI_ModelMaterialSlot.h"

namespace HIKARI {
class AssetRegistry;
struct MaterialAssetDescriptor;
struct ModelMaterialBindingAssetDescriptor;
} // namespace HIKARI

namespace HIKARI::ASSETS::MODELS {

struct ResolvedDefaultModelMaterialBinding {
    MaterialSlotId slotId{};
    uint32_t materialIndex = 0u;
    AssetGuid materialAssetGuid{};
    const MaterialAssetDescriptor* materialDescriptor = nullptr;
};

struct DefaultModelMaterialBindingResolution {
    const ModelMaterialBindingAssetDescriptor* bindingDescriptor = nullptr;
    std::vector<ResolvedDefaultModelMaterialBinding> bindings{};
    std::vector<std::string> diagnostics{};
    uint32_t matchingAssetCount = 0u;
};

// Resolves the model-level default layer only. Scene instance overrides
// are intentionally applied by the caller after this result.
DefaultModelMaterialBindingResolution ResolveDefaultModelMaterialBindings(
    const AssetRegistry& registry, const AssetGuid& modelAssetGuid,
    const std::vector<MaterialAsset>& importedMaterials);

// An explicit Material Set is exact-model-only. Without one, allowModelDefault
// selects the model's deterministic default binding asset.
DefaultModelMaterialBindingResolution ResolveModelMaterialBindings(const AssetRegistry& registry,
                                                                   const AssetGuid& modelAssetGuid,
                                                                   const AssetGuid& materialSetAssetGuid,
                                                                   const std::vector<MaterialAsset>& importedMaterials,
                                                                   bool allowModelDefault = true);

} // namespace HIKARI::ASSETS::MODELS
