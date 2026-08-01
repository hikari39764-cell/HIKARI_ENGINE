#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <string>

#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
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
        builder.AssetIdPicker(
            "Model Asset",
            AssetType::Model,
            assetId_);

        if (asset_ != nullptr && !asset_->materials.empty()) {
            builder.Text(
                "Material slots use imported material until an explicit "
                "override is assigned.");
            for (size_t materialIndex = 0u;
                materialIndex < asset_->materials.size();
                ++materialIndex) {

                const uint32_t slotIndex =
                    static_cast<uint32_t>(materialIndex);
                const MaterialAsset& imported =
                    asset_->materials[materialIndex];
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

                const std::string label =
                    "Material [" + std::to_string(slotIndex) + "] " +
                    (imported.name.empty()
                        ? std::string("Unnamed")
                        : imported.name);
                if (!builder.AssetIdPicker(
                        label,
                        AssetType::Material,
                        materialOverrideGuid)) {
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
        } else {
            builder.Text(
                "Load a model asset to inspect and bind its material slots.");
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
            oldPostGroupMask != postGroupMask_) {

            NotifyRenderStateDirty();
        }
    }

} // namespace HIKARI
