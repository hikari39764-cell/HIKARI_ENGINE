#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include "Core/Text/HIKARI_Localization.h"
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

        builder.Bool(
            AuthoringFieldDescriptor{
                ModelComponentPropertyIds::Visible,
                TEXT::Localize("ui.inspector.model.visible"),
                "Controls whether the model is submitted for rendering."
            },
            visible_);
        builder.Bool(
            AuthoringFieldDescriptor{
                ModelComponentPropertyIds::CastShadow,
                TEXT::Localize("ui.inspector.model.castShadow"),
                "Controls whether the model casts shadows."
            },
            castShadow_);
        builder.Bool(
            AuthoringFieldDescriptor{
                ModelComponentPropertyIds::ReceiveShadow,
                TEXT::Localize("ui.inspector.model.receiveShadow"),
                "Controls whether the model receives shadows."
            },
            receiveShadow_);
        builder.Bool(
            AuthoringFieldDescriptor{
                ModelComponentPropertyIds::RenderStatic,
                TEXT::Localize("ui.inspector.model.renderStatic"),
                "Marks the render surface as static authoring data."
            },
            renderStatic_);
        builder.Bool(
            AuthoringFieldDescriptor{
                ModelComponentPropertyIds::IncludeInLightingBake,
                TEXT::Localize(
                    "ui.inspector.model.includeInLightingBake"),
                "Includes the model surface in authored lighting bakes."
            },
            includeInLightingBake_);
        if (renderStatic_ && includeInLightingBake_) {
            builder.Text(
                TEXT::Localize(
                    "ui.inspector.model.surfaceGiHint"));
        }
        const bool modelAssetChanged = builder.AssetIdPicker(
            AuthoringFieldDescriptor{
                ModelComponentPropertyIds::AssetId,
                TEXT::Localize("ui.inspector.model.modelAsset"),
                "Model asset GUID assigned to this component."
            },
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
                        ? TEXT::Localize(
                            "ui.inspector.model.unnamed")
                        : imported.name,
                    imported.baseColorTexture.textureIndex >= 0
                });
            }
            materialSlots = runtimeSlots;
        }

        if (!assetId_.empty()) {
            std::vector<std::string> materialSetLabels{};
            std::vector<std::string> materialSetIds{};
            materialSetLabels.reserve(materialSets.size() + 3u);
            materialSetIds.reserve(materialSets.size() + 3u);
            materialSetLabels.push_back(
                TEXT::Localize(
                    "ui.inspector.model.importedModelMaterials"));
            materialSetIds.emplace_back();
            int selectedSetIndex = 0;
            const bool legacySelection =
                !materialSetSelectionAuthored_;
            if (legacySelection) {
                materialSetLabels.push_back(
                    TEXT::Localize(
                        "ui.inspector.model.legacyModelDefault"));
                materialSetIds.push_back("__legacy_model_default");
                selectedSetIndex = 1;
            }
            const size_t materialSetChoiceOffset =
                legacySelection ? 2u : 1u;
            for (size_t index = 0u; index < materialSets.size(); ++index) {
                const ModelMaterialSetAuthoringView& materialSet =
                    materialSets[index];
                materialSetLabels.push_back(
                    materialSet.name.empty()
                        ? TEXT::Localize(
                            "ui.inspector.model.unnamedMaterialSet")
                        : materialSet.name);
                materialSetIds.push_back(
                    materialSet.assetGuid.value);
                if (materialSet.assetGuid == materialSetAssetGuid_) {
                    selectedSetIndex = static_cast<int>(
                        index + materialSetChoiceOffset);
                }
            }
            if (materialSetAssetGuid_.IsValid() && selectedSetIndex == 0) {
                materialSetLabels.push_back(
                    TEXT::Localize(
                        "ui.inspector.model.missingMaterialSet"));
                materialSetIds.push_back(
                    materialSetAssetGuid_.value);
                selectedSetIndex =
                    static_cast<int>(materialSetLabels.size() - 1u);
            }
            std::vector<AuthoringChoice> materialSetChoices{};
            materialSetChoices.reserve(materialSetLabels.size());
            for (size_t index = 0u;
                index < materialSetLabels.size();
                ++index) {

                materialSetChoices.push_back({
                    materialSetIds[index],
                    materialSetLabels[index]
                });
            }
            if (builder.Choice(
                    AuthoringFieldDescriptor{
                        ModelComponentPropertyIds::MaterialSetAssetGuid,
                        TEXT::Localize(
                            "ui.inspector.model.materialSet"),
                        "Compatible whole-model Material Set asset GUID."
                    },
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
            builder.Section(
                TEXT::Localize(
                    "ui.inspector.model.advancedSlotOverrides"),
                false)) {
            builder.Text(
                TEXT::Localize(
                    "ui.inspector.model.slotOverridesHint"));
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
                    TEXT::Localize("ui.inspector.model.material") +
                    " [" + std::to_string(slotIndex) + "] " +
                    imported.name);
                builder.Text(imported.hasBaseColorTexture
                    ? TEXT::Localize(
                        "ui.inspector.model.importedDefaultBaseColorTexture")
                    : TEXT::Localize(
                        "ui.inspector.model.importedDefaultBaseColorFactor"));
                const std::string pickerLabel =
                    TEXT::Localize(
                        "ui.inspector.model.overrideMaterial") +
                    "##ModelMaterialSlot" +
                    std::to_string(slotIndex);
                const std::string materialOverridePropertyId =
                    "materialOverrides." +
                    (imported.slotId.IsValid()
                        ? ASSETS::MODELS::MaterialSlotIdToString(
                            imported.slotId)
                        : "legacy." + std::to_string(slotIndex));
                if (!builder.AssetIdPicker(
                        AuthoringFieldDescriptor{
                            materialOverridePropertyId,
                            pickerLabel,
                            "Per-instance Material asset override for this "
                            "stable imported model slot. Empty restores the "
                            "inherited material."
                        },
                        AssetType::Material,
                        materialOverrideGuid)) {
                    if (!materialOverrideGuid.empty() &&
                        builder.Button(
                            TEXT::Localize(
                                "ui.inspector.model.restoreImported") +
                            "##ModelMaterialSlot" +
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
                ? TEXT::Localize(
                    "ui.inspector.model.selectImportedHint")
                : slotDiagnostic);
        }

        int postMask = static_cast<int>(postGroupMask_);
        if (builder.Int(
                AuthoringFieldDescriptor{
                    ModelComponentPropertyIds::PostGroupMask,
                    TEXT::Localize(
                        "ui.inspector.model.postGroupMask"),
                    "Post-process group bit mask."
                },
                postMask)) {
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
