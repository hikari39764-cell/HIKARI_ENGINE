#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <algorithm>
#include <utility>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelBounds.h"
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

ModelAsset* ModelComponent::GetModelAsset() { return asset_; }

const ModelAsset* ModelComponent::GetModelAsset() const { return asset_; }

bool ModelComponent::QueryGeometryFit(GeometryFitDesc& outFit) const noexcept {
    if (asset_ == nullptr || !BOUNDS::IsUsable(asset_->bounds)) {
        return false;
    }

    const MATH::Vec3 rawSize = asset_->bounds.max - asset_->bounds.min;
    const MATH::Vec3 size{(std::max)(rawSize.x, 0.02f), (std::max)(rawSize.y, 0.02f), (std::max)(rawSize.z, 0.02f)};

    outFit = {};
    outFit.shape = GeometryFitShape::Box;
    outFit.center = (asset_->bounds.min + asset_->bounds.max) * 0.5f;
    outFit.size = size;
    outFit.radius = (std::min)({size.x, size.y, size.z}) * 0.5f;
    outFit.height = size.y;
    return true;
}

void ModelComponent::SetVisible(bool visible) {
    visible_ = visible;
    NotifyRenderStateDirty();
}

bool ModelComponent::IsVisible() const { return visible_; }

bool ModelComponent::GetCastShadow() const { return castShadow_; }

bool ModelComponent::GetReceiveShadow() const { return receiveShadow_; }

void ModelComponent::SetRenderStatic(bool enabled) {
    renderStatic_ = enabled;
    NotifyRenderStateDirty();
}

bool ModelComponent::IsRenderStatic() const { return renderStatic_; }

void ModelComponent::SetIncludeInLightingBake(bool enabled) {
    if (includeInLightingBake_ == enabled) {
        return;
    }
    includeInLightingBake_ = enabled;
    NotifyRenderStateDirty();
}

bool ModelComponent::GetIncludeInLightingBake() const { return includeInLightingBake_; }

ModelRenderDebugMode ModelComponent::GetRenderDebugMode() const { return debugRenderMode_; }

uint32_t ModelComponent::GetWireColor() const { return wireColor_; }

uint32_t ModelComponent::GetMaxWireLines() const { return maxWireLines_; }

bool ModelComponent::GetWirePerPrimitiveColor() const { return wirePerPrimitiveColor_; }

const std::string& ModelComponent::GetAssetId() const { return assetId_; }

void ModelComponent::SetAssetId(std::string assetId) {
    if (assetId_ != assetId) {
        materialSetAssetGuid_ = {};
        materialSetSelectionAuthored_ = true;
        materialOverrides_.clear();
        ClearRuntimeMaterialOverrides();
        runtimeNodeVisibilityOverrides_.clear();
    }
    assetId_ = std::move(assetId);
    NotifyRenderStateDirty();
}

const AssetGuid& ModelComponent::GetMaterialSetAssetGuid() const noexcept { return materialSetAssetGuid_; }

bool ModelComponent::HasAuthoredMaterialSetSelection() const noexcept { return materialSetSelectionAuthored_; }

void ModelComponent::SetMaterialSetAssetGuid(AssetGuid materialSetGuid) {
    if (materialSetAssetGuid_ == materialSetGuid && materialSetSelectionAuthored_) {
        return;
    }
    materialSetAssetGuid_ = std::move(materialSetGuid);
    materialSetSelectionAuthored_ = true;
    ClearRuntimeMaterialOverrides();
    NotifyRenderStateDirty();
}

void ModelComponent::ClearMaterialSetAssetGuid() { SetMaterialSetAssetGuid({}); }

void ModelComponent::UseImportedModelMaterials() { ClearMaterialSetAssetGuid(); }

uint32_t ModelComponent::GetPostGroupMask() const { return runtimePostGroupMask_.value_or(postGroupMask_); }

bool ModelComponent::SetRuntimePostGroupMask(uint32_t mask) {
    if ((mask & 0x80000000u) != 0u)
        return false;
    if (runtimePostGroupMask_ != mask) {
        runtimePostGroupMask_ = mask;
        NotifyRenderStateDirty();
    }
    return true;
}

void ModelComponent::ClearRuntimePostGroupMask() {
    if (runtimePostGroupMask_) {
        runtimePostGroupMask_.reset();
        NotifyRenderStateDirty();
    }
}

const std::vector<ModelMaterialOverrideSlot>& ModelComponent::GetMaterialOverrides() const {
    return materialOverrides_;
}

void ModelComponent::SetMaterialOverride(MaterialSlotId slotId, AssetGuid materialGuid) {
    if (!slotId.IsValid()) {
        return;
    }
    const auto existing =
        std::find_if(materialOverrides_.begin(), materialOverrides_.end(),
                     [slotId](const ModelMaterialOverrideSlot& slot) { return slot.slotId == slotId; });
    if (existing != materialOverrides_.end()) {
        existing->materialAssetGuid = std::move(materialGuid);
    } else {
        materialOverrides_.push_back(ModelMaterialOverrideSlot{slotId, std::move(materialGuid)});
    }
    ClearRuntimeMaterialOverride(slotId);
}

void ModelComponent::ClearMaterialOverride(MaterialSlotId slotId) {
    if (std::erase_if(materialOverrides_,
                      [slotId](const ModelMaterialOverrideSlot& slot) { return slot.slotId == slotId; }) != 0u) {
        ClearRuntimeMaterialOverride(slotId);
    }
}

void ModelComponent::ClearRuntimeMaterialOverride(MaterialSlotId slotId) {
    if (asset_ != nullptr) {
        if (const auto index = ASSETS::MODELS::FindMaterialSlotIndex(asset_->materials, slotId)) {
            ClearRuntimeMaterialOverride(*index);
        }
    }
}

Material* ModelComponent::GetRuntimeMaterialOverride(uint32_t slotIndex) {
    for (RuntimeMaterialOverrideSlot& slot : runtimeMaterialOverrides_) {
        if (slot.materialIndex == slotIndex) {
            return slot.material.get();
        }
    }
    return nullptr;
}

const Material* ModelComponent::GetRuntimeMaterialOverride(uint32_t slotIndex) const {
    for (const RuntimeMaterialOverrideSlot& slot : runtimeMaterialOverrides_) {
        if (slot.materialIndex == slotIndex) {
            return slot.material.get();
        }
    }
    return nullptr;
}

void ModelComponent::SetRuntimeMaterialOverride(uint32_t slotIndex, std::unique_ptr<Material> material) {
    if (material == nullptr) {
        ClearRuntimeMaterialOverride(slotIndex);
        return;
    }

    for (RuntimeMaterialOverrideSlot& slot : runtimeMaterialOverrides_) {
        if (slot.materialIndex == slotIndex) {
            slot.material = std::move(material);
            NotifyRenderStateDirty();
            return;
        }
    }

    runtimeMaterialOverrides_.push_back(RuntimeMaterialOverrideSlot{slotIndex, std::move(material)});
    NotifyRenderStateDirty();
}

void ModelComponent::ClearRuntimeMaterialOverride(uint32_t slotIndex) {
    std::erase_if(runtimeMaterialOverrides_,
                  [slotIndex](const RuntimeMaterialOverrideSlot& slot) { return slot.materialIndex == slotIndex; });
    NotifyRenderStateDirty();
}

void ModelComponent::ClearRuntimeMaterialOverrides() {
    runtimeMaterialOverrides_.clear();
    NotifyRenderStateDirty();
}

const std::vector<RuntimeMaterialOverrideSlot>& ModelComponent::GetRuntimeMaterialOverrides() const noexcept {
    return runtimeMaterialOverrides_;
}

bool ModelComponent::SetRuntimeNodeVisibilityOverride(std::string_view nodeName, bool visible,
                                                      bool includeDescendants) {
    if (nodeName.empty()) {
        return false;
    }

    const auto found = std::find_if(
        runtimeNodeVisibilityOverrides_.begin(), runtimeNodeVisibilityOverrides_.end(),
        [nodeName](const RuntimeModelNodeVisibilityOverride& entry) { return entry.nodeName == nodeName; });
    if (found != runtimeNodeVisibilityOverrides_.end()) {
        if (found->visible == visible && found->includeDescendants == includeDescendants) {
            return true;
        }
        found->visible = visible;
        found->includeDescendants = includeDescendants;
        NotifyRenderStateDirty();
        return true;
    }

    runtimeNodeVisibilityOverrides_.push_back(RuntimeModelNodeVisibilityOverride{
        std::string(nodeName),
        visible,
        includeDescendants,
    });
    NotifyRenderStateDirty();
    return true;
}

bool ModelComponent::ClearRuntimeNodeVisibilityOverride(std::string_view nodeName) {
    if (nodeName.empty()) {
        return false;
    }
    const size_t previousSize = runtimeNodeVisibilityOverrides_.size();
    std::erase_if(runtimeNodeVisibilityOverrides_,
                  [nodeName](const RuntimeModelNodeVisibilityOverride& entry) { return entry.nodeName == nodeName; });
    if (runtimeNodeVisibilityOverrides_.size() == previousSize) {
        return false;
    }
    NotifyRenderStateDirty();
    return true;
}

void ModelComponent::ClearRuntimeNodeVisibilityOverrides() {
    if (runtimeNodeVisibilityOverrides_.empty()) {
        return;
    }
    runtimeNodeVisibilityOverrides_.clear();
    NotifyRenderStateDirty();
}

const std::vector<RuntimeModelNodeVisibilityOverride>& ModelComponent::GetRuntimeNodeVisibilityOverrides()
    const noexcept {
    return runtimeNodeVisibilityOverrides_;
}

std::vector<uint8_t> ModelComponent::ResolveRuntimeNodeVisibilityMask(const ModelAsset& model) const {
    if (model.nodes.empty() || runtimeNodeVisibilityOverrides_.empty()) {
        return {};
    }

    std::vector<const RuntimeModelNodeVisibilityOverride*> directOverrides(model.nodes.size(), nullptr);
    for (const RuntimeModelNodeVisibilityOverride& entry : runtimeNodeVisibilityOverrides_) {
        const auto node = std::find_if(model.nodes.begin(), model.nodes.end(), [&entry](const ModelNode& candidate) {
            return candidate.name == entry.nodeName;
        });
        if (node == model.nodes.end()) {
            continue;
        }
        const size_t nodeIndex = static_cast<size_t>(node - model.nodes.begin());
        directOverrides[nodeIndex] = &entry;
    }

    std::vector<uint8_t> mask(model.nodes.size(), 1u);
    bool hasHiddenNode = false;
    for (size_t nodeIndex = 0u; nodeIndex < model.nodes.size(); ++nodeIndex) {
        const RuntimeModelNodeVisibilityOverride* direct = directOverrides[nodeIndex];
        if (direct != nullptr) {
            mask[nodeIndex] = direct->visible ? 1u : 0u;
        } else {
            int parent = model.nodes[nodeIndex].parent;
            for (size_t depth = 0u;
                 depth < model.nodes.size() && parent >= 0 && static_cast<size_t>(parent) < model.nodes.size();
                 ++depth) {
                const size_t parentIndex = static_cast<size_t>(parent);
                const RuntimeModelNodeVisibilityOverride* inherited = directOverrides[parentIndex];
                if (inherited != nullptr && inherited->includeDescendants) {
                    mask[nodeIndex] = inherited->visible ? 1u : 0u;
                    break;
                }
                const int nextParent = model.nodes[parentIndex].parent;
                if (nextParent == parent) {
                    break;
                }
                parent = nextParent;
            }
        }
        hasHiddenNode = hasHiddenNode || mask[nodeIndex] == 0u;
    }

    if (!hasHiddenNode) {
        return {};
    }
    return mask;
}

} // namespace HIKARI
