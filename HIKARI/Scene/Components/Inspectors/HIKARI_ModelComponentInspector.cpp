// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <span>
#include <string>
#include <utility>
#include <vector>

#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "RenderContracts/Model/HIKARI_ModelMaterialSlot.h"
#include "Authoring/HIKARI_FieldInspector.h"
#include "Authoring/HIKARI_IModelMaterialSlotProvider.h"
#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI {

void ModelComponent::BuildInspector(IInspectorBuilder& builder) {
    ModelSettings next = settings_;
    if (InspectFields(builder, next, kModelFields)) {
        // モデルを替えると、材質セットと上書きは前のモデルのスロットを指すので捨てる。SetAssetId が前のモデルと比べて
        // 捨てるので、ここでは前の ID を残したまま他の項目を置き換える。
        std::string assetId = std::move(next.assetId);
        next.assetId = settings_.assetId;
        settings_ = NormalizeModelSettings(std::move(next));
        SetAssetId(std::move(assetId));
    }

    // ここから先は、モデルの材質スロットごとに決まる欄（材質セットの選択と、スロットごとの上書き）。
    std::vector<ModelMaterialSlotAuthoringView> runtimeSlots{};
    std::span<const ModelMaterialSlotAuthoringView> materialSlots{};
    std::string slotDiagnostic{};
    std::span<const ModelMaterialSetAuthoringView> materialSets{};
    std::string materialSetDiagnostic{};
    const InspectorContext& context = builder.GetContext();
    const std::string& assetId = settings_.assetId;
    if (context.modelMaterialSlotProvider != nullptr && context.assetDatabase != nullptr && !assetId.empty()) {
        materialSlots = context.modelMaterialSlotProvider->ResolveMaterialSlots(*context.assetDatabase,
                                                                                AssetGuid{assetId}, slotDiagnostic);
        materialSets = context.modelMaterialSlotProvider->ResolveCompatibleMaterialSets(
            *context.assetDatabase, AssetGuid{assetId}, materialSetDiagnostic);
    } else if (asset_ != nullptr) {
        runtimeSlots.reserve(asset_->materials.size());
        for (size_t materialIndex = 0u; materialIndex < asset_->materials.size(); ++materialIndex) {
            const MaterialAsset& imported = asset_->materials[materialIndex];
            runtimeSlots.push_back(ModelMaterialSlotAuthoringView{
                static_cast<uint32_t>(materialIndex), imported.slotId,
                imported.name.empty() ? TEXT::Localize("ui.inspector.model.unnamed") : imported.name,
                imported.baseColorTexture.textureIndex >= 0});
        }
        materialSlots = runtimeSlots;
    }

    const bool proceduralMaterialSlots = assetId.empty() && context.proceduralMeshSettings != nullptr;
    if (proceduralMaterialSlots) {
        runtimeSlots.push_back(ModelMaterialSlotAuthoringView{
            0u,
            ASSETS::MODELS::MakeMaterialSlotId(ASSETS::MODELS::MaterialSlotImporterSemantic::Procedural,
                                               "Procedural Default", 0u),
            TEXT::Localize("ui.inspector.model.proceduralMaterial"), true});
        materialSlots = runtimeSlots;
    }

    const bool oldMaterialSetSelectionAuthored = materialSetSelectionAuthored_;
    const AssetGuid oldMaterialSetAssetGuid = materialSetAssetGuid_;
    if (!assetId.empty()) {
        std::vector<std::string> materialSetLabels{};
        std::vector<std::string> materialSetIds{};
        materialSetLabels.reserve(materialSets.size() + 3u);
        materialSetIds.reserve(materialSets.size() + 3u);
        materialSetLabels.push_back(TEXT::Localize("ui.inspector.model.importedModelMaterials"));
        materialSetIds.emplace_back();
        int selectedSetIndex = 0;
        const bool modelDefaultSelection = !materialSetSelectionAuthored_;
        if (modelDefaultSelection) {
            materialSetLabels.push_back(TEXT::Localize("ui.inspector.model.modelDefault"));
            materialSetIds.push_back("__model_default");
            selectedSetIndex = 1;
        }
        const size_t materialSetChoiceOffset = modelDefaultSelection ? 2u : 1u;
        for (size_t index = 0u; index < materialSets.size(); ++index) {
            const ModelMaterialSetAuthoringView& materialSet = materialSets[index];
            materialSetLabels.push_back(
                materialSet.name.empty() ? TEXT::Localize("ui.inspector.model.unnamedMaterialSet") : materialSet.name);
            materialSetIds.push_back(materialSet.assetGuid.value);
            if (materialSet.assetGuid == materialSetAssetGuid_) {
                selectedSetIndex = static_cast<int>(index + materialSetChoiceOffset);
            }
        }
        if (materialSetAssetGuid_.IsValid() && selectedSetIndex == 0) {
            materialSetLabels.push_back(TEXT::Localize("ui.inspector.model.missingMaterialSet"));
            materialSetIds.push_back(materialSetAssetGuid_.value);
            selectedSetIndex = static_cast<int>(materialSetLabels.size() - 1u);
        }
        std::vector<AuthoringChoice> materialSetChoices{};
        materialSetChoices.reserve(materialSetLabels.size());
        for (size_t index = 0u; index < materialSetLabels.size(); ++index) {
            materialSetChoices.push_back({materialSetIds[index], materialSetLabels[index]});
        }
        if (builder.Choice(AuthoringFieldDescriptor{"materialSetAssetGuid",
                                                    TEXT::Localize("ui.inspector.model.materialSet"),
                                                    "Compatible whole-model Material Set asset GUID."},
                           selectedSetIndex, materialSetChoices)) {
            if (selectedSetIndex == 0) {
                UseImportedModelMaterials();
            } else if (modelDefaultSelection && selectedSetIndex == 1) {
                // 明示的な Material Set を選ぶまでモデル既定のバインディングを使う。
            } else {
                const size_t setIndex = static_cast<size_t>(selectedSetIndex) - materialSetChoiceOffset;
                if (setIndex < materialSets.size()) {
                    SetMaterialSetAssetGuid(materialSets[setIndex].assetGuid);
                } else {
                    UseImportedModelMaterials();
                }
            }
        }
        if (materialSets.empty() && !materialSetDiagnostic.empty()) {
            builder.Text(materialSetDiagnostic);
        }
    }

    const auto drawMaterialSlotOverride = [this, &builder](const ModelMaterialSlotAuthoringView& imported) {
        const uint32_t slotIndex = imported.slotIndex;
        std::string materialOverrideGuid{};
        for (const ModelMaterialOverrideSlot& slot : materialOverrides_) {
            if (imported.slotId.IsValid() && slot.slotId == imported.slotId) {
                materialOverrideGuid = slot.materialAssetGuid.value;
                break;
            }
        }

        builder.Text(TEXT::Localize("ui.inspector.model.material") + " [" + std::to_string(slotIndex) + "] " +
                     imported.name);
        builder.Text(imported.hasBaseColorTexture
                         ? TEXT::Localize("ui.inspector.model.importedDefaultBaseColorTexture")
                         : TEXT::Localize("ui.inspector.model.importedDefaultBaseColorFactor"));
        const std::string pickerLabel =
            TEXT::Localize("ui.inspector.model.overrideMaterial") + "##ModelMaterialSlot" + std::to_string(slotIndex);
        if (!imported.slotId.IsValid()) {
            return;
        }
        const std::string materialOverridePropertyId =
            "materialOverrides." + ASSETS::MODELS::MaterialSlotIdToString(imported.slotId);
        if (!builder.AssetIdPicker(AuthoringFieldDescriptor{materialOverridePropertyId, pickerLabel,
                                                            "Per-instance Material asset override for this "
                                                            "stable model slot. Empty restores the "
                                                            "inherited material."},
                                   AssetType::Material, materialOverrideGuid)) {
            if (!materialOverrideGuid.empty() && builder.Button(TEXT::Localize("ui.inspector.model.restoreImported") +
                                                                "##ModelMaterialSlot" + std::to_string(slotIndex))) {
                ClearMaterialOverride(imported.slotId);
            }
            return;
        }
        if (materialOverrideGuid.empty()) {
            ClearMaterialOverride(imported.slotId);
        } else {
            SetMaterialOverride(imported.slotId, AssetGuid{materialOverrideGuid});
        }
    };

    if (proceduralMaterialSlots) {
        // プロシージャル形状のマテリアルは Model コンポーネントでは
        // 管理しない。ProceduralMeshComponent 側の選択が所有する。
        builder.Text(TEXT::Localize("ui.inspector.model.proceduralMaterialInProcedural"));
    } else if (!materialSlots.empty() &&
               builder.Section(TEXT::Localize("ui.inspector.model.advancedSlotOverrides"), false)) {
        builder.Text(TEXT::Localize("ui.inspector.model.slotOverridesHint"));
        for (const ModelMaterialSlotAuthoringView& imported : materialSlots) {
            drawMaterialSlotOverride(imported);
        }
    } else if (materialSlots.empty()) {
        builder.Text(slotDiagnostic.empty() ? TEXT::Localize("ui.inspector.model.selectImportedHint") : slotDiagnostic);
    }

    if (oldMaterialSetAssetGuid != materialSetAssetGuid_ ||
        oldMaterialSetSelectionAuthored != materialSetSelectionAuthored_) {
        NotifyRenderStateDirty();
    }
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
