#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <algorithm>
#include <utility>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

    ModelComponent::~ModelComponent() = default;

    void ModelComponent::NotifyRenderStateDirty() {
        if (GameObject* owner = GetOwner()) {
            owner->MarkRenderStateDirty();
        }
    }

    void ModelComponent::SetModelAsset(ModelAsset* asset) {
        asset_ = asset;
        if (asset_ != nullptr) {
            assetId_ = asset_->GetName();
        }
        NotifyRenderStateDirty();
    }

    ModelAsset* ModelComponent::GetModelAsset() {
        return asset_;
    }

    const ModelAsset* ModelComponent::GetModelAsset() const {
        return asset_;
    }

    bool ModelComponent::QueryGeometryFit(
        GeometryFitDesc& outFit) const noexcept {

        if (asset_ == nullptr || !BOUNDS::IsUsable(asset_->bounds)) {
            return false;
        }

        const MATH::Vec3 rawSize =
            asset_->bounds.max - asset_->bounds.min;
        const MATH::Vec3 size{
            (std::max)(rawSize.x, 0.02f),
            (std::max)(rawSize.y, 0.02f),
            (std::max)(rawSize.z, 0.02f)
        };

        outFit = {};
        outFit.shape = GeometryFitShape::Box;
        outFit.center =
            (asset_->bounds.min + asset_->bounds.max) * 0.5f;
        outFit.size = size;
        outFit.radius = (std::min)({
            size.x,
            size.y,
            size.z
        }) * 0.5f;
        outFit.height = size.y;
        return true;
    }

    void ModelComponent::SetVisible(bool visible) {
        visible_ = visible;
        NotifyRenderStateDirty();
    }

    bool ModelComponent::IsVisible() const {
        return visible_;
    }

    void ModelComponent::SetSkeletonDebugVisible(bool visible) {
        showSkeletonDebug_ = visible;
        NotifyRenderStateDirty();
    }

    bool ModelComponent::IsSkeletonDebugVisible() const {
        return showSkeletonDebug_;
    }

    void ModelComponent::SetSkeletonDebugXRay(bool enabled) {
        skeletonDebugXRay_ = enabled;
        NotifyRenderStateDirty();
    }

    bool ModelComponent::IsSkeletonDebugXRay() const {
        return skeletonDebugXRay_;
    }

    void ModelComponent::SetCastShadow(bool enabled) {
        castShadow_ = enabled;
        NotifyRenderStateDirty();
    }

    bool ModelComponent::GetCastShadow() const {
        return castShadow_;
    }

    void ModelComponent::SetReceiveShadow(bool enabled) {
        receiveShadow_ = enabled;
        NotifyRenderStateDirty();
    }

    bool ModelComponent::GetReceiveShadow() const {
        return receiveShadow_;
    }

    void ModelComponent::SetRenderStatic(bool enabled) {
        renderStatic_ = enabled;
        NotifyRenderStateDirty();
    }

    bool ModelComponent::IsRenderStatic() const {
        return renderStatic_;
    }

    void ModelComponent::SetIncludeInLightingBake(bool enabled) {
        if (includeInLightingBake_ == enabled) {
            return;
        }
        includeInLightingBake_ = enabled;
        NotifyRenderStateDirty();
    }

    bool ModelComponent::GetIncludeInLightingBake() const {
        return includeInLightingBake_;
    }

    void ModelComponent::SetRenderDebugMode(
        ModelRenderDebugMode mode) {

        debugRenderMode_ = mode;
        NotifyRenderStateDirty();
    }

    ModelRenderDebugMode ModelComponent::GetRenderDebugMode() const {
        return debugRenderMode_;
    }

    void ModelComponent::SetWireColor(uint32_t color) {
        wireColor_ = color;
        NotifyRenderStateDirty();
    }

    uint32_t ModelComponent::GetWireColor() const {
        return wireColor_;
    }

    void ModelComponent::SetMaxWireLines(uint32_t count) {
        maxWireLines_ = count;
        NotifyRenderStateDirty();
    }

    uint32_t ModelComponent::GetMaxWireLines() const {
        return maxWireLines_;
    }

    bool ModelComponent::GetWirePerPrimitiveColor() const {
        return wirePerPrimitiveColor_;
    }

    const std::string& ModelComponent::GetAssetId() const {
        return assetId_;
    }

    void ModelComponent::SetAssetId(std::string assetId) {
        if (assetId_ != assetId) {
            materialSetAssetGuid_ = {};
            materialSetSelectionAuthored_ = true;
            materialOverrides_.clear();
            ClearRuntimeMaterialOverrides();
        }
        assetId_ = std::move(assetId);
        NotifyRenderStateDirty();
    }

    const AssetGuid& ModelComponent::GetMaterialSetAssetGuid() const noexcept {
        return materialSetAssetGuid_;
    }

    bool ModelComponent::HasAuthoredMaterialSetSelection() const noexcept {
        return materialSetSelectionAuthored_;
    }

    void ModelComponent::SetMaterialSetAssetGuid(
        AssetGuid materialSetGuid) {

        if (materialSetAssetGuid_ == materialSetGuid &&
            materialSetSelectionAuthored_) {
            return;
        }
        materialSetAssetGuid_ = std::move(materialSetGuid);
        materialSetSelectionAuthored_ = true;
        ClearRuntimeMaterialOverrides();
        NotifyRenderStateDirty();
    }

    void ModelComponent::ClearMaterialSetAssetGuid() {
        SetMaterialSetAssetGuid({});
    }

    void ModelComponent::UseImportedModelMaterials() {
        ClearMaterialSetAssetGuid();
    }

    void ModelComponent::SetPostGroupMask(uint32_t mask) {
        postGroupMask_ = mask;
        NotifyRenderStateDirty();
    }

    uint32_t ModelComponent::GetPostGroupMask() const {
        return postGroupMask_;
    }

    const std::vector<ModelMaterialOverrideSlot>&
        ModelComponent::GetMaterialOverrides() const {

        return materialOverrides_;
    }

    void ModelComponent::SetMaterialOverride(
        uint32_t slotIndex,
        AssetGuid materialGuid) {

        SetMaterialOverride(
            ResolveCurrentSlotId(slotIndex),
            slotIndex,
            std::move(materialGuid));
    }

    void ModelComponent::SetMaterialOverride(
        MaterialSlotId slotId,
        uint32_t legacySlotIndex,
        AssetGuid materialGuid) {

        auto existing = materialOverrides_.end();
        if (slotId.IsValid()) {
            existing = std::find_if(
                materialOverrides_.begin(),
                materialOverrides_.end(),
                [slotId](const ModelMaterialOverrideSlot& slot) {
                    return slot.slotId == slotId;
                });
        }
        if (existing == materialOverrides_.end()) {
            existing = std::find_if(
                materialOverrides_.begin(),
                materialOverrides_.end(),
                [legacySlotIndex](
                    const ModelMaterialOverrideSlot& slot) {

                    return !slot.slotId.IsValid() &&
                        slot.slotIndex == legacySlotIndex;
                });
        }
        if (existing != materialOverrides_.end()) {
            existing->slotId = slotId;
            existing->slotIndex = legacySlotIndex;
            existing->materialAssetGuid = std::move(materialGuid);
            ClearRuntimeMaterialOverride(legacySlotIndex);
            return;
        }

        materialOverrides_.push_back(ModelMaterialOverrideSlot{
            legacySlotIndex,
            std::move(materialGuid),
            slotId
        });
        ClearRuntimeMaterialOverride(legacySlotIndex);
    }

    void ModelComponent::ClearMaterialOverride(uint32_t slotIndex) {
        const MaterialSlotId slotId = ResolveCurrentSlotId(slotIndex);
        std::erase_if(
            materialOverrides_,
            [slotIndex, slotId](const ModelMaterialOverrideSlot& slot) {
                if (slotId.IsValid()) {
                    return slot.slotId == slotId ||
                        (!slot.slotId.IsValid() &&
                            slot.slotIndex == slotIndex);
                }
                return slot.slotIndex == slotIndex;
            });
        ClearRuntimeMaterialOverride(slotIndex);
    }

    void ModelComponent::ClearMaterialOverride(MaterialSlotId slotId) {
        if (!slotId.IsValid()) {
            return;
        }
        const auto found = std::find_if(
            materialOverrides_.begin(),
            materialOverrides_.end(),
            [slotId](const ModelMaterialOverrideSlot& slot) {
                return slot.slotId == slotId;
            });
        if (found == materialOverrides_.end()) {
            return;
        }
        const uint32_t legacySlotIndex = found->slotIndex;
        materialOverrides_.erase(found);
        ClearRuntimeMaterialOverride(legacySlotIndex);
    }

    const Material* ModelComponent::GetRuntimeMaterialOverride(
        uint32_t slotIndex) const {

        for (const RuntimeMaterialOverrideSlot& slot :
            runtimeMaterialOverrides_) {

            if (slot.materialIndex == slotIndex) {
                return slot.material.get();
            }
        }
        return nullptr;
    }

    void ModelComponent::SetRuntimeMaterialOverride(
        uint32_t slotIndex,
        std::unique_ptr<Material> material) {

        if (material == nullptr) {
            ClearRuntimeMaterialOverride(slotIndex);
            return;
        }

        for (RuntimeMaterialOverrideSlot& slot :
            runtimeMaterialOverrides_) {

            if (slot.materialIndex == slotIndex) {
                slot.material = std::move(material);
                NotifyRenderStateDirty();
                return;
            }
        }

        runtimeMaterialOverrides_.push_back(
            RuntimeMaterialOverrideSlot{
                slotIndex,
                std::move(material)
            });
        NotifyRenderStateDirty();
    }

    void ModelComponent::ClearRuntimeMaterialOverride(
        uint32_t slotIndex) {

        std::erase_if(
            runtimeMaterialOverrides_,
            [slotIndex](const RuntimeMaterialOverrideSlot& slot) {
                return slot.materialIndex == slotIndex;
            });
        NotifyRenderStateDirty();
    }

    void ModelComponent::ClearRuntimeMaterialOverrides() {
        runtimeMaterialOverrides_.clear();
        NotifyRenderStateDirty();
    }

    const std::vector<RuntimeMaterialOverrideSlot>&
        ModelComponent::GetRuntimeMaterialOverrides() const noexcept {

        return runtimeMaterialOverrides_;
    }

    MaterialSlotId ModelComponent::ResolveCurrentSlotId(
        uint32_t slotIndex) const noexcept {

        if (asset_ == nullptr || slotIndex >= asset_->materials.size()) {
            return {};
        }
        return asset_->materials[slotIndex].slotId;
    }

} // namespace HIKARI
