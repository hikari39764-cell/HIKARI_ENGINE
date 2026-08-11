#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Assets/Models/HIKARI_ModelMaterialSlot.h"
#include "Render3D/Core/HIKARI_Material.h"

namespace HIKARI {

    namespace {
        const char* ToString(ModelRenderDebugMode mode) {
            switch (mode) {
            case ModelRenderDebugMode::WireOverlay:
                return "WireOverlay";
            case ModelRenderDebugMode::WireOnly:
                return "WireOnly";
            case ModelRenderDebugMode::BoundsOnly:
                return "BoundsOnly";
            case ModelRenderDebugMode::Normal:
            default:
                return "Normal";
            }
        }

        ModelRenderDebugMode ParseRenderDebugMode(
            const nlohmann::json& node,
            ModelRenderDebugMode fallback) {

            const std::string value =
                node.is_string()
                    ? node.get<std::string>()
                    : std::string{};
            if (value == "WireOverlay") {
                return ModelRenderDebugMode::WireOverlay;
            }
            if (value == "WireOnly") {
                return ModelRenderDebugMode::WireOnly;
            }
            if (value == "BoundsOnly") {
                return ModelRenderDebugMode::BoundsOnly;
            }
            if (value == "Normal") {
                return ModelRenderDebugMode::Normal;
            }
            return fallback;
        }
    } // namespace

    void ModelComponent::Serialize(nlohmann::json& out) const {
        out[ModelComponentPropertyIds::AssetId] = assetId_;
        out[ModelComponentPropertyIds::Visible] = visible_;
        out["showSkeletonDebug"] = showSkeletonDebug_;
        out["skeletonDebugXRay"] = skeletonDebugXRay_;
        out[ModelComponentPropertyIds::CastShadow] = castShadow_;
        out[ModelComponentPropertyIds::ReceiveShadow] = receiveShadow_;
        out[ModelComponentPropertyIds::RenderStatic] = renderStatic_;
        out[ModelComponentPropertyIds::IncludeInLightingBake] =
            includeInLightingBake_;
        out["debugRenderMode"] = ToString(debugRenderMode_);
        out["wireColor"] = wireColor_;
        out["maxWireLines"] = maxWireLines_;
        out["wirePerPrimitiveColor"] = wirePerPrimitiveColor_;
        out[ModelComponentPropertyIds::PostGroupMask] = postGroupMask_;
        if (materialSetSelectionAuthored_) {
            out["materialSetAssetGuid"] = materialSetAssetGuid_.value;
        }
        out["materialOverrides"] = nlohmann::json::array();
        for (const ModelMaterialOverrideSlot& slot :
            materialOverrides_) {

            if (!slot.materialAssetGuid.IsValid()) {
                continue;
            }
            nlohmann::json node{
                { "slot", slot.slotIndex },
                { "legacySlotIndex", slot.slotIndex },
                { "materialAssetGuid", slot.materialAssetGuid.value }
            };
            if (slot.slotId.IsValid()) {
                node["slotId"] =
                    ASSETS::MODELS::MaterialSlotIdToString(slot.slotId);
            }
            out["materialOverrides"].push_back(std::move(node));
        }
    }

    void ModelComponent::Deserialize(const nlohmann::json& in) {
        assetId_ = in.value(
            ModelComponentPropertyIds::AssetId,
            assetId_);
        visible_ = in.value(
            ModelComponentPropertyIds::Visible,
            visible_);
        showSkeletonDebug_ =
            in.value("showSkeletonDebug", showSkeletonDebug_);
        skeletonDebugXRay_ =
            in.value("skeletonDebugXRay", skeletonDebugXRay_);
        castShadow_ = in.value(
            ModelComponentPropertyIds::CastShadow,
            castShadow_);
        receiveShadow_ = in.value(
            ModelComponentPropertyIds::ReceiveShadow,
            receiveShadow_);
        renderStatic_ = in.value(
            ModelComponentPropertyIds::RenderStatic,
            renderStatic_);
        includeInLightingBake_ = in.value(
            ModelComponentPropertyIds::IncludeInLightingBake,
            includeInLightingBake_);
        debugRenderMode_ = ParseRenderDebugMode(
            in.value("debugRenderMode", nlohmann::json{}),
            debugRenderMode_);
        wireColor_ = in.value("wireColor", wireColor_);
        maxWireLines_ = in.value("maxWireLines", maxWireLines_);
        wirePerPrimitiveColor_ =
            in.value("wirePerPrimitiveColor", wirePerPrimitiveColor_);
        postGroupMask_ =
            in.value(
                ModelComponentPropertyIds::PostGroupMask,
                postGroupMask_);
        materialSetSelectionAuthored_ =
            in.contains("materialSetAssetGuid");
        materialSetAssetGuid_.value = materialSetSelectionAuthored_
            ? in.value("materialSetAssetGuid", std::string{})
            : std::string{};

        materialOverrides_.clear();
        if (in.contains("materialOverrides") &&
            in["materialOverrides"].is_array()) {

            for (const nlohmann::json& node :
                in["materialOverrides"]) {

                if (!node.is_object()) {
                    continue;
                }

                const uint32_t slotIndex = node.value(
                    "legacySlotIndex",
                    node.value("slot", 0u));
                MaterialSlotId slotId{};
                if (const auto parsedSlotId =
                        ASSETS::MODELS::ParseMaterialSlotId(
                            node.value("slotId", std::string{}))) {

                    slotId = *parsedSlotId;
                }
                AssetGuid materialGuid{};
                materialGuid.value =
                    node.value("materialAssetGuid", std::string{});
                if (!materialGuid.IsValid()) {
                    continue;
                }

                const auto existing = std::find_if(
                    materialOverrides_.begin(),
                    materialOverrides_.end(),
                    [slotIndex, slotId](
                        const ModelMaterialOverrideSlot& slot) {

                        if (slotId.IsValid() && slot.slotId.IsValid()) {
                            return slot.slotId == slotId;
                        }
                        return !slotId.IsValid() &&
                            !slot.slotId.IsValid() &&
                            slot.slotIndex == slotIndex;
                    });
                if (existing != materialOverrides_.end()) {
                    existing->slotId = slotId;
                    existing->slotIndex = slotIndex;
                    existing->materialAssetGuid = std::move(materialGuid);
                } else {
                    materialOverrides_.push_back(
                        ModelMaterialOverrideSlot{
                            slotIndex,
                            std::move(materialGuid),
                            slotId
                        });
                }
            }
        }

        runtimeMaterialOverrides_.clear();
        NotifyRenderStateDirty();
    }

} // namespace HIKARI
