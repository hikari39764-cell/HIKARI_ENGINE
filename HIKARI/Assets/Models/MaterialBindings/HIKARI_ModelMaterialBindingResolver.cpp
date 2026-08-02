#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingResolver.h"

#include <algorithm>
#include <limits>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI::ASSETS::MODELS {
    namespace {
        bool BindingDescriptorLess(
            const ModelMaterialBindingAssetDescriptor* left,
            const ModelMaterialBindingAssetDescriptor* right) {

            if (left->sourcePath != right->sourcePath) {
                return left->sourcePath < right->sourcePath;
            }
            return left->id.value < right->id.value;
        }
    }

    DefaultModelMaterialBindingResolution ResolveModelMaterialBindings(
        const AssetRegistry& registry,
        const AssetGuid& modelAssetGuid,
        const AssetGuid& materialSetAssetGuid,
        const std::vector<MaterialAsset>& importedMaterials,
        bool allowLegacyDefault) {

        DefaultModelMaterialBindingResolution result{};
        if (!modelAssetGuid.IsValid() || importedMaterials.empty()) {
            return result;
        }

        std::vector<const ModelMaterialBindingAssetDescriptor*> candidates{};
        if (materialSetAssetGuid.IsValid()) {
            const auto* explicitSet =
                registry.FindAs<ModelMaterialBindingAssetDescriptor>(
                    AssetId{materialSetAssetGuid.value});
            if (explicitSet == nullptr) {
                result.diagnostics.push_back(
                    "selected Material Set is unavailable: " +
                    materialSetAssetGuid.value);
                return result;
            }
            if (explicitSet->data.modelAssetGuid != modelAssetGuid) {
                result.diagnostics.push_back(
                    "selected Material Set belongs to model " +
                    explicitSet->data.modelAssetGuid.value +
                    " and was rejected for model " + modelAssetGuid.value);
                return result;
            }
            candidates.push_back(explicitSet);
        } else if (allowLegacyDefault) {
            for (const AssetDescriptor* descriptor :
                registry.CollectByType(AssetType::ModelMaterialBinding)) {

                const auto* binding =
                    dynamic_cast<const ModelMaterialBindingAssetDescriptor*>(
                        descriptor);
                if (binding != nullptr &&
                    binding->data.modelAssetGuid == modelAssetGuid) {
                    candidates.push_back(binding);
                }
            }
            std::sort(candidates.begin(), candidates.end(), BindingDescriptorLess);
        }
        result.matchingAssetCount = static_cast<uint32_t>(candidates.size());
        if (candidates.empty()) {
            return result;
        }

        if (!materialSetAssetGuid.IsValid() && candidates.size() > 1u) {
            result.diagnostics.push_back(
                "multiple Material Sets match this model; an explicit component selection is required");
            return result;
        }

        result.bindingDescriptor = candidates.front();

        result.bindings.reserve(result.bindingDescriptor->data.bindings.size());
        for (const ModelMaterialBindingEntry& authored :
            result.bindingDescriptor->data.bindings) {

            const MaterialSlotResolution slot = ResolveMaterialSlot(
                importedMaterials,
                authored.slotId,
                (std::numeric_limits<uint32_t>::max)());
            if (!slot.IsValid()) {
                result.diagnostics.push_back(
                    "default material binding references a missing stable slot " +
                    MaterialSlotIdToString(authored.slotId));
                continue;
            }
            const auto* material = registry.FindAs<MaterialAssetDescriptor>(
                AssetId{authored.materialAssetGuid.value});
            if (material == nullptr) {
                result.diagnostics.push_back(
                    "default material binding references an unavailable material " +
                    authored.materialAssetGuid.value);
                continue;
            }
            result.bindings.push_back(ResolvedDefaultModelMaterialBinding{
                authored.slotId,
                slot.materialIndex,
                authored.materialAssetGuid,
                material
            });
        }
        std::sort(
            result.bindings.begin(),
            result.bindings.end(),
            [](const auto& left, const auto& right) {
                return left.materialIndex < right.materialIndex;
            });
        return result;
    }

    DefaultModelMaterialBindingResolution ResolveDefaultModelMaterialBindings(
        const AssetRegistry& registry,
        const AssetGuid& modelAssetGuid,
        const std::vector<MaterialAsset>& importedMaterials) {

        return ResolveModelMaterialBindings(
            registry,
            modelAssetGuid,
            {},
            importedMaterials);
    }

} // namespace HIKARI::ASSETS::MODELS
