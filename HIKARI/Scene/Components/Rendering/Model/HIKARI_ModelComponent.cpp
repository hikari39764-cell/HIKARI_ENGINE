#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "RenderContracts/Model/HIKARI_ModelMaterialSlot.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

ModelSettings NormalizeModelSettings(ModelSettings settings) {
    settings = JSONFIELDS::Sanitize(settings, kModelFields);
    settings.postGroupMask &= 0x7fffffffu;
    return settings;
}

bool DrawModelWireColor(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field, uint32_t& value,
                        const ModelSettings&) {
    MATH::Vec4 color = RENDERER3D::DEBUG::DecodeRgba(value);
    if (!builder.Color3(field, color.x, color.y, color.z)) {
        return false;
    }
    value = RENDERER3D::DEBUG::EncodeRgba(color);
    return true;
}

ModelComponent::~ModelComponent() = default;

void ModelComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kModelFields);
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
    ModelSettings decoded{};
    JSONFIELDS::Read(in, decoded, kModelFields);

    // 材質セットは、項目があること自体が「作者が選んだ」印（無ければモデル既定のバインディング）。
    const nlohmann::json* materialSet = JSONREAD::FindMember(in, "materialSetAssetGuid");
    if (materialSet != nullptr && !materialSet->is_string()) {
        throw std::runtime_error("materialSetAssetGuid: expects a string");
    }
    std::vector<ModelMaterialOverrideSlot> overrides{};
    if (const nlohmann::json* list = JSONREAD::FindMember(in, "materialOverrides");
        list != nullptr && !list->is_null()) {
        if (!list->is_array()) {
            throw std::runtime_error("materialOverrides: expects an array");
        }
        for (size_t index = 0; index < list->size(); ++index) {
            const nlohmann::json& node = (*list)[index];
            const std::string path = "materialOverrides[" + std::to_string(index) + "]";
            const nlohmann::json* slotText = JSONREAD::FindMember(node, "slotId");
            const nlohmann::json* guidText = JSONREAD::FindMember(node, "materialAssetGuid");
            if (slotText == nullptr || guidText == nullptr || !slotText->is_string() || !guidText->is_string()) {
                throw std::runtime_error(path + ": expects an object with string slotId and materialAssetGuid");
            }
            const auto slotId = ASSETS::MODELS::ParseMaterialSlotId(slotText->get<std::string>());
            AssetGuid materialGuid{guidText->get<std::string>()};
            if (!slotId.has_value() || !materialGuid.IsValid()) {
                throw std::runtime_error(path + ": slotId or materialAssetGuid is invalid");
            }
            // 同じスロットが重なったら後の指定を採る。
            const auto existing =
                std::find_if(overrides.begin(), overrides.end(),
                             [&](const ModelMaterialOverrideSlot& slot) { return slot.slotId == *slotId; });
            if (existing != overrides.end()) {
                existing->materialAssetGuid = std::move(materialGuid);
            } else {
                overrides.push_back(ModelMaterialOverrideSlot{*slotId, std::move(materialGuid)});
            }
        }
    }

    settings_ = NormalizeModelSettings(std::move(decoded));
    runtimePostGroupMask_.reset();
    materialSetSelectionAuthored_ = materialSet != nullptr;
    materialSetAssetGuid_.value = materialSet != nullptr ? materialSet->get<std::string>() : std::string{};
    materialOverrides_ = std::move(overrides);
    runtimeMaterialOverrides_.clear();
    runtimeNodeVisibilityOverrides_.clear();
    NotifyRenderStateDirty();
}

void ModelComponent::SetModelAsset(ModelAsset* asset) {
    asset_ = asset;
    if (asset_ != nullptr) {
        settings_.assetId = asset_->GetName();
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
    settings_.visible = visible;
    NotifyRenderStateDirty();
}

bool ModelComponent::IsVisible() const { return settings_.visible; }

bool ModelComponent::GetCastShadow() const { return settings_.castShadow; }

bool ModelComponent::GetReceiveShadow() const { return settings_.receiveShadow; }

void ModelComponent::SetRenderStatic(bool enabled) {
    settings_.renderStatic = enabled;
    NotifyRenderStateDirty();
}

bool ModelComponent::IsRenderStatic() const { return settings_.renderStatic; }

void ModelComponent::SetIncludeInLightingBake(bool enabled) {
    if (settings_.includeInLightingBake == enabled) {
        return;
    }
    settings_.includeInLightingBake = enabled;
    NotifyRenderStateDirty();
}

bool ModelComponent::GetIncludeInLightingBake() const { return settings_.includeInLightingBake; }

ModelRenderDebugMode ModelComponent::GetRenderDebugMode() const { return settings_.debugRenderMode; }

uint32_t ModelComponent::GetWireColor() const { return settings_.wireColor; }

uint32_t ModelComponent::GetMaxWireLines() const { return settings_.maxWireLines; }

bool ModelComponent::GetWirePerPrimitiveColor() const { return settings_.wirePerPrimitiveColor; }

const std::string& ModelComponent::GetAssetId() const { return settings_.assetId; }

void ModelComponent::SetAssetId(std::string assetId) {
    if (settings_.assetId != assetId) {
        materialSetAssetGuid_ = {};
        materialSetSelectionAuthored_ = true;
        materialOverrides_.clear();
        ClearRuntimeMaterialOverrides();
        runtimeNodeVisibilityOverrides_.clear();
    }
    settings_.assetId = std::move(assetId);
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

uint32_t ModelComponent::GetPostGroupMask() const { return runtimePostGroupMask_.value_or(settings_.postGroupMask); }

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
    // 材質の原地変更（運行時の束縛など）も描画の変更として届ける。渲染同期は毎フレーム全物体を見ない。
    material->SetRevisionListener(
        [](void* owner) noexcept { static_cast<ModelComponent*>(owner)->NotifyRenderStateDirty(); }, this);

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
