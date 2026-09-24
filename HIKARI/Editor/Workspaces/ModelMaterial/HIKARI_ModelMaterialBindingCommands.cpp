#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::RefreshSelectedSlotPresentation() {
    if (!previewScene_.IsReady()) {
        return;
    }
    // Selection is preview-session surface state. It must never replace or
    // mutate the imported/authored material bound to a slot.
    if (!selectedMaterialIndex_.has_value()) {
        previewScene_.ClearEditorSelectionOutline();
        return;
    }
    const SlotState* selected = FindSlotByMaterialIndex(*selectedMaterialIndex_);
    if (selected == nullptr) {
        previewScene_.ClearEditorSelectionOutline();
        return;
    }
    (void)previewScene_.SetEditorSelectionOutline(selected->view.materialIndex);
}

std::shared_ptr<Material> ModelMaterialWorkspaceController::BuildPreviewMaterial(const AssetGuid& materialGuid,
                                                                                 const AssetDatabase& assetDatabase,
                                                                                 const AssetRegistry& assetRegistry,
                                                                                 std::string& outMessage) {
    const AssetRecord* record = assetDatabase.FindByGuid(materialGuid);
    if (record == nullptr || record->type != AssetType::Material) {
        outMessage = "selected material asset is no longer available";
        return {};
    }

    const auto* descriptor = assetRegistry.FindAs<MaterialAssetDescriptor>(AssetId{materialGuid.value});
    if (descriptor == nullptr) {
        outMessage = "material is not imported into the runtime registry";
        return {};
    }
    if (descriptor->sourceData.domain != MaterialDomain::Surface) {
        outMessage = "Particle materials cannot be bound to model surface slots";
        return {};
    }

    auto material = std::make_shared<Material>();
    MaterialRuntimeBuilder builder{};
    if (!builder.BuildRuntimeMaterial(descriptor->sourceData, descriptor->compileResult, assetRegistry,
                                      materialProgramRegistry_, *material,
                                      materialGuid.value + "/model-material-preview")) {
        outMessage = "runtime material projection failed; imported fallback kept";
        return {};
    }
    material->SetInheritModelTextures(true);
    return material;
}

ModelMaterialBindingCommandResult ModelMaterialWorkspaceController::ExecuteBindingCommand(
    const ModelMaterialBindingCommand& command, const AssetDatabase& assetDatabase,
    const AssetRegistry& assetRegistry) {
    ModelMaterialBindingCommandResult result{};
    if (!IsEditingModel()) {
        result.message = "open a model before editing material bindings";
        return result;
    }
    if (!command.slotId.IsValid()) {
        result.message = "the selected slot has no stable slotId; reimport model";
        return result;
    }

    SlotState* slot = FindSlot(command.slotId);
    if (slot == nullptr) {
        result.message = "material slot no longer exists after model refresh";
        return result;
    }
    result.accepted = true;
    result.beforeMaterialGuid = slot->view.overrideMaterialGuid;

    ModelMaterialWorkspaceDocument::BindingMap beforeBindings = authoringDocument_.GetBindings();
    ModelMaterialWorkspaceDocument::BindingMap& bindings = authoringDocument_.EditBindings();
    if (command.type == ModelMaterialBindingCommandType::RestoreImportedFallback) {
        if (!slot->view.overrideMaterialGuid.IsValid() &&
            slot->view.source == ModelMaterialBindingSource::ImportedFallback) {
            result.afterMaterialGuid = {};
            result.message = "slot already uses the imported model material";
            return result;
        }
        if (!previewScene_.ClearPreviewMaterialOverride(slot->view.materialIndex)) {
            result.accepted = false;
            result.message = "preview rejected the selected material slot";
            return result;
        }

        bindings.erase(slot->view.slotId.value);
        slot->previewMaterial.reset();
        slot->view.overrideMaterialGuid = {};
        slot->view.source = ModelMaterialBindingSource::ImportedFallback;
        slot->view.diagnostic.clear();
        result.changed = result.beforeMaterialGuid.IsValid();
        result.afterMaterialGuid = {};
        if (result.changed) {
            (void)authoringDocument_.CommitBindings(std::move(beforeBindings), "Restore imported material");
        }
        result.message = "restored imported model material; save bindings to persist";
        UpdatePublicSlotViews();
        RefreshSelectedSlotPresentation();
        return result;
    }

    if (!command.materialAssetGuid.IsValid()) {
        result.accepted = false;
        result.message = "choose a material asset before binding";
        return result;
    }
    if (slot->view.overrideMaterialGuid == command.materialAssetGuid && slot->previewMaterial != nullptr) {
        result.afterMaterialGuid = command.materialAssetGuid;
        result.message = "selected material is already bound to this slot";
        return result;
    }

    std::string buildMessage{};
    std::shared_ptr<Material> previewMaterial =
        BuildPreviewMaterial(command.materialAssetGuid, assetDatabase, assetRegistry, buildMessage);
    if (previewMaterial == nullptr) {
        result.accepted = false;
        result.message = std::move(buildMessage);
        return result;
    }
    if (!previewScene_.SetPreviewMaterialOverride(slot->view.materialIndex, previewMaterial.get())) {
        result.accepted = false;
        result.message = "preview rejected the selected material slot";
        return result;
    }

    bindings[slot->view.slotId.value] = command.materialAssetGuid;
    slot->previewMaterial = std::move(previewMaterial);
    slot->view.overrideMaterialGuid = command.materialAssetGuid;
    slot->view.source = ModelMaterialBindingSource::AuthoredOverride;
    slot->view.diagnostic.clear();
    result.changed = result.beforeMaterialGuid != command.materialAssetGuid;
    result.afterMaterialGuid = command.materialAssetGuid;
    if (result.changed) {
        (void)authoringDocument_.CommitBindings(std::move(beforeBindings), "Bind material asset");
    }
    result.message = "material binding changed; save bindings to persist";
    UpdatePublicSlotViews();
    RefreshSelectedSlotPresentation();
    return result;
}

bool ModelMaterialWorkspaceController::RebuildPreviewOverride(SlotState& slot, const AssetDatabase& assetDatabase,
                                                              const AssetRegistry& assetRegistry,
                                                              std::string& outMessage) {
    std::shared_ptr<Material> previewMaterial =
        BuildPreviewMaterial(slot.view.overrideMaterialGuid, assetDatabase, assetRegistry, outMessage);
    if (previewMaterial == nullptr ||
        !previewScene_.SetPreviewMaterialOverride(slot.view.materialIndex, previewMaterial.get())) {
        slot.previewMaterial.reset();
        slot.view.source = ModelMaterialBindingSource::OverrideUnavailable;
        slot.view.diagnostic = outMessage.empty() ? "preview override could not be applied" : outMessage;
        return false;
    }
    slot.previewMaterial = std::move(previewMaterial);
    slot.view.source = ModelMaterialBindingSource::AuthoredOverride;
    slot.view.diagnostic.clear();
    return true;
}

} // namespace HIKARI::EDITOR
