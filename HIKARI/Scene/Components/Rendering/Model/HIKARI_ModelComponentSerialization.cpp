#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <algorithm>
#include <string>
#include <utility>

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
        out["assetId"] = assetId_;
        out["visible"] = visible_;
        out["showSkeletonDebug"] = showSkeletonDebug_;
        out["skeletonDebugXRay"] = skeletonDebugXRay_;
        out["castShadow"] = castShadow_;
        out["receiveShadow"] = receiveShadow_;
        out["renderStatic"] = renderStatic_;
        out["debugRenderMode"] = ToString(debugRenderMode_);
        out["wireColor"] = wireColor_;
        out["maxWireLines"] = maxWireLines_;
        out["wirePerPrimitiveColor"] = wirePerPrimitiveColor_;
        out["postGroupMask"] = postGroupMask_;
        out["materialOverrides"] = nlohmann::json::array();
        for (const ModelMaterialOverrideSlot& slot :
            materialOverrides_) {

            if (!slot.materialAssetGuid.IsValid()) {
                continue;
            }
            out["materialOverrides"].push_back({
                { "slot", slot.slotIndex },
                { "materialAssetGuid", slot.materialAssetGuid.value }
            });
        }
    }

    void ModelComponent::Deserialize(const nlohmann::json& in) {
        assetId_ = in.value("assetId", assetId_);
        visible_ = in.value("visible", visible_);
        showSkeletonDebug_ =
            in.value("showSkeletonDebug", showSkeletonDebug_);
        skeletonDebugXRay_ =
            in.value("skeletonDebugXRay", skeletonDebugXRay_);
        castShadow_ = in.value("castShadow", castShadow_);
        receiveShadow_ = in.value("receiveShadow", receiveShadow_);
        renderStatic_ = in.value("renderStatic", renderStatic_);
        debugRenderMode_ = ParseRenderDebugMode(
            in.value("debugRenderMode", nlohmann::json{}),
            debugRenderMode_);
        wireColor_ = in.value("wireColor", wireColor_);
        maxWireLines_ = in.value("maxWireLines", maxWireLines_);
        wirePerPrimitiveColor_ =
            in.value("wirePerPrimitiveColor", wirePerPrimitiveColor_);
        postGroupMask_ =
            in.value("postGroupMask", postGroupMask_);

        materialOverrides_.clear();
        if (in.contains("materialOverrides") &&
            in["materialOverrides"].is_array()) {

            for (const nlohmann::json& node :
                in["materialOverrides"]) {

                if (!node.is_object()) {
                    continue;
                }

                ModelMaterialOverrideSlot slot{};
                slot.slotIndex = node.value("slot", 0u);
                slot.materialAssetGuid.value =
                    node.value("materialAssetGuid", std::string{});
                if (slot.materialAssetGuid.IsValid()) {
                    materialOverrides_.push_back(std::move(slot));
                }
            }
        }

        runtimeMaterialOverride_.reset();
        runtimeMaterialOverrideGuid_ = {};
        NotifyRenderStateDirty();
    }

} // namespace HIKARI
