#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <string>

#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    void ModelComponent::BuildInspector(
        IInspectorBuilder& builder) {

        const bool oldVisible = visible_;
        const bool oldCastShadow = castShadow_;
        const bool oldReceiveShadow = receiveShadow_;
        const bool oldRenderStatic = renderStatic_;
        const std::string oldAssetId = assetId_;
        const uint32_t oldPostGroupMask = postGroupMask_;

        builder.Bool("Visible", visible_);
        builder.Bool("Cast Shadow", castShadow_);
        builder.Bool("Receive Shadow", receiveShadow_);
        builder.Bool("Render Static", renderStatic_);
        builder.AssetIdPicker(
            "Model Asset",
            AssetType::Model,
            assetId_);

        std::string materialOverrideGuid{};
        for (const ModelMaterialOverrideSlot& slot :
            materialOverrides_) {

            if (slot.slotIndex == 0 &&
                slot.materialAssetGuid.IsValid()) {

                materialOverrideGuid =
                    slot.materialAssetGuid.value;
                break;
            }
        }

        if (builder.AssetIdPicker(
            "Material",
            AssetType::Material,
            materialOverrideGuid)) {

            if (materialOverrideGuid.empty()) {
                ClearMaterialOverride(0);
            } else {
                SetMaterialOverride(
                    0,
                    AssetGuid{ materialOverrideGuid });
            }
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
            oldAssetId != assetId_ ||
            oldPostGroupMask != postGroupMask_) {

            NotifyRenderStateDirty();
        }
    }

} // namespace HIKARI
