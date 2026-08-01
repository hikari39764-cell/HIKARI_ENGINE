#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Assets/Models/HIKARI_ModelMaterialSlot.h"

namespace HIKARI {
    class AssetRegistry;
    struct MaterialAssetDescriptor;
    struct ModelMaterialBindingAssetDescriptor;
}

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

        bool HasBindingAsset() const noexcept {
            return bindingDescriptor != nullptr;
        }
    };

    // Resolves the model-level default layer only. Scene instance overrides
    // are intentionally applied by the caller after this result.
    DefaultModelMaterialBindingResolution ResolveDefaultModelMaterialBindings(
        const AssetRegistry& registry,
        const AssetGuid& modelAssetGuid,
        const std::vector<MaterialAsset>& importedMaterials);

} // namespace HIKARI::ASSETS::MODELS
