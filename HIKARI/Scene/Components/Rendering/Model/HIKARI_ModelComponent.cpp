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
        assetId_ = std::move(assetId);
        NotifyRenderStateDirty();
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

        for (ModelMaterialOverrideSlot& slot : materialOverrides_) {
            if (slot.slotIndex == slotIndex) {
                slot.materialAssetGuid = std::move(materialGuid);
                ClearRuntimeMaterialOverride();
                return;
            }
        }

        materialOverrides_.push_back(ModelMaterialOverrideSlot{
            slotIndex,
            std::move(materialGuid)
        });
        ClearRuntimeMaterialOverride();
    }

    void ModelComponent::ClearMaterialOverride(uint32_t slotIndex) {
        std::erase_if(
            materialOverrides_,
            [slotIndex](const ModelMaterialOverrideSlot& slot) {
                return slot.slotIndex == slotIndex;
            });
        ClearRuntimeMaterialOverride();
    }

    const Material* ModelComponent::GetRuntimeMaterialOverride() const {
        return runtimeMaterialOverride_.get();
    }

    void ModelComponent::SetRuntimeMaterialOverride(
        std::unique_ptr<Material> material,
        AssetGuid guid) {

        runtimeMaterialOverride_ = std::move(material);
        runtimeMaterialOverrideGuid_ = std::move(guid);
        NotifyRenderStateDirty();
    }

    void ModelComponent::ClearRuntimeMaterialOverride() {
        runtimeMaterialOverride_.reset();
        runtimeMaterialOverrideGuid_ = {};
        NotifyRenderStateDirty();
    }

    const AssetGuid&
        ModelComponent::GetRuntimeMaterialOverrideGuid() const {

        return runtimeMaterialOverrideGuid_;
    }

} // namespace HIKARI
