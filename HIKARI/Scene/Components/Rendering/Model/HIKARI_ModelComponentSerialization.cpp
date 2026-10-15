#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Assets/Models/HIKARI_ModelMaterialSlot.h"
#include "Core/HIKARI_JsonRead.h"
#include "Render3D/Core/HIKARI_Material.h"

namespace HIKARI {

void ModelComponent::Serialize(nlohmann::json& out) const {
    out[ModelComponentPropertyIds::AssetId] = assetId_;
    out[ModelComponentPropertyIds::Visible] = visible_;
    out["showSkeletonDebug"] = showSkeletonDebug_;
    out["skeletonDebugXRay"] = skeletonDebugXRay_;
    out[ModelComponentPropertyIds::CastShadow] = castShadow_;
    out[ModelComponentPropertyIds::ReceiveShadow] = receiveShadow_;
    out[ModelComponentPropertyIds::RenderStatic] = renderStatic_;
    out[ModelComponentPropertyIds::IncludeInLightingBake] = includeInLightingBake_;
    out["debugRenderMode"] = TEXT::EnumToName(kModelRenderDebugModeNames, debugRenderMode_, "Normal");
    out["wireColor"] = wireColor_;
    out["maxWireLines"] = maxWireLines_;
    out["wirePerPrimitiveColor"] = wirePerPrimitiveColor_;
    out[ModelComponentPropertyIds::PostGroupMask] = postGroupMask_;
    if (materialSetSelectionAuthored_) {
        out["materialSetAssetGuid"] = materialSetAssetGuid_.value;
    }
    out["materialOverrides"] = nlohmann::json::array();
    for (const ModelMaterialOverrideSlot& slot : materialOverrides_) {
        if (!slot.materialAssetGuid.IsValid() || !slot.slotId.IsValid()) {
            continue;
        }
        out["materialOverrides"].push_back({{"slotId", ASSETS::MODELS::MaterialSlotIdToString(slot.slotId)},
                                            {"materialAssetGuid", slot.materialAssetGuid.value}});
    }
}

void ModelComponent::Deserialize(const nlohmann::json& in) {
    assetId_ = in.value(ModelComponentPropertyIds::AssetId, assetId_);
    visible_ = in.value(ModelComponentPropertyIds::Visible, visible_);
    showSkeletonDebug_ = in.value("showSkeletonDebug", showSkeletonDebug_);
    skeletonDebugXRay_ = in.value("skeletonDebugXRay", skeletonDebugXRay_);
    castShadow_ = in.value(ModelComponentPropertyIds::CastShadow, castShadow_);
    receiveShadow_ = in.value(ModelComponentPropertyIds::ReceiveShadow, receiveShadow_);
    renderStatic_ = in.value(ModelComponentPropertyIds::RenderStatic, renderStatic_);
    includeInLightingBake_ = in.value(ModelComponentPropertyIds::IncludeInLightingBake, includeInLightingBake_);
    debugRenderMode_ =
        JSONREAD::EnumOr(in.value("debugRenderMode", nlohmann::json{}), kModelRenderDebugModeNames, debugRenderMode_);
    wireColor_ = in.value("wireColor", wireColor_);
    maxWireLines_ = in.value("maxWireLines", maxWireLines_);
    wirePerPrimitiveColor_ = in.value("wirePerPrimitiveColor", wirePerPrimitiveColor_);
    postGroupMask_ = in.value(ModelComponentPropertyIds::PostGroupMask, postGroupMask_) & 0x7fffffffu;
    runtimePostGroupMask_.reset();
    materialSetSelectionAuthored_ = in.contains("materialSetAssetGuid");
    materialSetAssetGuid_.value =
        materialSetSelectionAuthored_ ? in.value("materialSetAssetGuid", std::string{}) : std::string{};

    materialOverrides_.clear();
    if (in.contains("materialOverrides") && in["materialOverrides"].is_array()) {
        for (const nlohmann::json& node : in["materialOverrides"]) {
            if (!node.is_object()) {
                continue;
            }

            const auto slotId = ASSETS::MODELS::ParseMaterialSlotId(node.value("slotId", std::string{}));
            AssetGuid materialGuid{node.value("materialAssetGuid", std::string{})};
            if (!slotId.has_value() || !materialGuid.IsValid()) {
                continue;
            }
            const auto existing =
                std::find_if(materialOverrides_.begin(), materialOverrides_.end(),
                             [&](const ModelMaterialOverrideSlot& slot) { return slot.slotId == *slotId; });
            if (existing != materialOverrides_.end()) {
                existing->materialAssetGuid = std::move(materialGuid);
            } else {
                materialOverrides_.push_back(ModelMaterialOverrideSlot{*slotId, std::move(materialGuid)});
            }
        }
    }

    runtimeMaterialOverrides_.clear();
    runtimeNodeVisibilityOverrides_.clear();
    NotifyRenderStateDirty();
}

} // namespace HIKARI
