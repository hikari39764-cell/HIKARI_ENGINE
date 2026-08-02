#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <span>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Authoring/HIKARI_IModelMaterialSlotProvider.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    void ModelComponent::BuildInspector(
        IInspectorBuilder& builder) {

        const bool oldVisible = visible_;
        const bool oldCastShadow = castShadow_;
        const bool oldReceiveShadow = receiveShadow_;
        const bool oldRenderStatic = renderStatic_;
        const bool oldIncludeInLightingBake =
            includeInLightingBake_;
        const std::string oldAssetId = assetId_;
        const AssetGuid oldMaterialSetAssetGuid =
            materialSetAssetGuid_;
        const bool oldMaterialSetSelectionAuthored =
            materialSetSelectionAuthored_;
        const uint32_t oldPostGroupMask = postGroupMask_;

        builder.Bool("Visible", visible_);
        builder.Bool("Cast Shadow", castShadow_);
        builder.Bool("Receive Shadow", receiveShadow_);
        builder.Bool("Render Static", renderStatic_);
        builder.Bool(
            "Include In Lighting Bake",
            includeInLightingBake_);
        if (renderStatic_ && includeInLightingBake_) {
            builder.Text(
                "Surface GI: UV1 must be unique and inside 0-1. "
                "If invalid, Probe / Sky fallback remains active.");
        }
        const bool modelAssetChanged = builder.AssetIdPicker(
            "Model Asset",
            AssetType::Model,
            assetId_);
        if (modelAssetChanged) {
            materialSetAssetGuid_ = {};
            materialSetSelectionAuthored_ = true;
            materialOverrides_.clear();
            ClearRuntimeMaterialOverrides();
        }

        std::vector<ModelMaterialSlotAuthoringView> runtimeSlots{};
        std::span<const ModelMaterialSlotAuthoringView> materialSlots{};
        std::string slotDiagnostic{};
        std::span<const ModelMaterialSetAuthoringView> materialSets{};
        std::string materialSetDiagnostic{};
        const InspectorContext& context = builder.GetContext();
        if (context.modelMaterialSlotProvider != nullptr &&
            context.assetDatabase != nullptr &&
            !assetId_.empty()) {
            materialSlots =
                context.modelMaterialSlotProvider->ResolveMaterialSlots(
                    *context.assetDatabase,
                    AssetGuid{ assetId_ },
                    slotDiagnostic);
            materialSets =
                context.modelMaterialSlotProvider->ResolveCompatibleMaterialSets(
                    *context.assetDatabase,
                    AssetGuid{ assetId_ },
                    materialSetDiagnostic);
        } else if (asset_ != nullptr) {
            runtimeSlots.reserve(asset_->materials.size());
            for (size_t materialIndex = 0u;
                materialIndex < asset_->materials.size();
                ++materialIndex) {
                const MaterialAsset& imported =
                    asset_->materials[materialIndex];
                runtimeSlots.push_back(ModelMaterialSlotAuthoringView{
                    static_cast<uint32_t>(materialIndex),
                    imported.slotId,
                    imported.name.empty()
                        ? "Unnamed"
                        : imported.name,
                    imported.baseColorTexture.textureIndex >= 0
                });
            }
            materialSlots = runtimeSlots;
        }

        if (!assetId_.empty()) {
            std::vector<std::string> materialSetLabels{};
            std::vector<const char*> materialSetChoices{};
            materialSetLabels.reserve(materialSets.size() + 3u);
            materialSetLabels.push_back("Imported Model Materials");
            int selectedSetIndex = 0;
            const bool legacySelection =
                !materialSetSelectionAuthored_;
            if (legacySelection) {
                materialSetLabels.push_back(
                    "Legacy Model Default (migration)");
                selectedSetIndex = 1;
            }
            const size_t materialSetChoiceOffset =
                legacySelection ? 2u : 1u;
            for (size_t index = 0u; index < materialSets.size(); ++index) {
                const ModelMaterialSetAuthoringView& materialSet =
                    materialSets[index];
                materialSetLabels.push_back(
                    materialSet.name.empty()
                        ? "Unnamed Material Set"
                        : materialSet.name);
                if (materialSet.assetGuid == materialSetAssetGuid_) {
                    selectedSetIndex = static_cast<int>(
                        index + materialSetChoiceOffset);
                }
            }
            if (materialSetAssetGuid_.IsValid() && selectedSetIndex == 0) {
                materialSetLabels.push_back(
                    "Missing or Incompatible Material Set");
                selectedSetIndex =
                    static_cast<int>(materialSetLabels.size() - 1u);
            }
            materialSetChoices.reserve(materialSetLabels.size());
            for (const std::string& label : materialSetLabels) {
                materialSetChoices.push_back(label.c_str());
            }
            if (builder.Choice(
                    "Material Set",
                    selectedSetIndex,
                    materialSetChoices)) {
                if (selectedSetIndex == 0) {
                    UseImportedModelMaterials();
                } else if (legacySelection && selectedSetIndex == 1) {
                    // Keep the pre-Material-Set compatibility behavior until
                    // the user deliberately chooses a new mode.
                } else {
                    const size_t setIndex =
                        static_cast<size_t>(selectedSetIndex) -
                        materialSetChoiceOffset;
                    if (setIndex < materialSets.size()) {
                        SetMaterialSetAssetGuid(
                            materialSets[setIndex].assetGuid);
                    } else {
                        UseImportedModelMaterials();
                    }
                }
            }
            if (materialSets.empty() && !materialSetDiagnostic.empty()) {
                builder.Text(materialSetDiagnostic);
            }
        }

        if (!materialSlots.empty() &&
            builder.Section("Advanced Slot Overrides", false)) {
            builder.Text(
                "These per-slot overrides apply after the whole Material Set.");
            for (const ModelMaterialSlotAuthoringView& imported :
                materialSlots) {

                const uint32_t slotIndex = imported.slotIndex;
                std::string materialOverrideGuid{};
                for (const ModelMaterialOverrideSlot& slot :
                    materialOverrides_) {

                    const bool stableMatch =
                        imported.slotId.IsValid() &&
                        slot.slotId == imported.slotId;
                    const bool legacyMatch =
                        !slot.slotId.IsValid() &&
                        slot.slotIndex == slotIndex;
                    if (stableMatch || legacyMatch) {
                        materialOverrideGuid =
                            slot.materialAssetGuid.value;
                        break;
                    }
                }

                builder.Text(
                    "Material [" + std::to_string(slotIndex) + "] " +
                    imported.name);
                builder.Text(imported.hasBaseColorTexture
                    ? "Imported default: Base Color texture"
                    : "Imported default: Base Color factor");
                const std::string pickerLabel =
                    "Override Material##ModelMaterialSlot" +
                    std::to_string(slotIndex);
                if (!builder.AssetIdPicker(
                        pickerLabel,
                        AssetType::Material,
                        materialOverrideGuid)) {
                    if (!materialOverrideGuid.empty() &&
                        builder.Button(
                            "Restore Imported##ModelMaterialSlot" +
                            std::to_string(slotIndex))) {
                        ClearMaterialOverride(slotIndex);
                    }
                    continue;
                }
                if (materialOverrideGuid.empty()) {
                    ClearMaterialOverride(slotIndex);
                } else {
                    SetMaterialOverride(
                        imported.slotId,
                        slotIndex,
                        AssetGuid{ materialOverrideGuid });
                }
            }
        } else if (materialSlots.empty()) {
            builder.Text(slotDiagnostic.empty()
                ? "Select an imported model to inspect and bind its material slots."
                : slotDiagnostic);
        }

        int postMask = static_cast<int>(postGroupMask_);
        if (builder.Int("Post Group Mask", postMask)) {
            postGroupMask_ =
                static_cast<uint32_t>(postMask < 0 ? 0 : postMask);
        }

        if (oldVisible != visible_ ||
            oldCastShadow != castShadow_ ||
            oldReceiveShadow != receiveShadow_ ||
            oldRenderStatic != renderStatic_ ||
            oldIncludeInLightingBake != includeInLightingBake_ ||
            oldAssetId != assetId_ ||
            oldMaterialSetAssetGuid != materialSetAssetGuid_ ||
            oldMaterialSetSelectionAuthored !=
                materialSetSelectionAuthored_ ||
            oldPostGroupMask != postGroupMask_) {

            NotifyRenderStateDirty();
        }
    }

} // namespace HIKARI
