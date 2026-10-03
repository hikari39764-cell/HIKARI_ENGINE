#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"

#include <algorithm>
#include <iterator>
#include <span>
#include <string>
#include <utility>

#include "Assets/Shader/HIKARI_ShaderParameterSerialization.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialSerialization.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialStore.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {
namespace {

const ASSETS::SHADER::ShaderParameterDefinition* FindParameterDefinition(const SPRITE::SpriteMaterialBinding& binding,
                                                                         std::string_view key) noexcept {
    if (!binding.shaderArtifact) {
        return nullptr;
    }
    const auto found =
        std::find_if(binding.shaderArtifact->parameters.begin(), binding.shaderArtifact->parameters.end(),
                     [key](const auto& definition) { return definition.key == key; });
    return found != binding.shaderArtifact->parameters.end() ? &*found : nullptr;
}

bool BuildEffectiveSlots(const SPRITE::SpriteMaterialBinding& binding,
                         std::span<const ASSETS::SHADER::ShaderParameterOverride> overrides,
                         ASSETS::SHADER::ShaderParameterSlots& outSlots, std::string& outError) {
    if (!binding.shaderArtifact || !binding.asset) {
        outError = "SpriteMaterial binding is incomplete.";
        return false;
    }
    ASSETS::SHADER::ShaderParameterSlots slots = binding.parameterSlots;
    for (const ASSETS::SHADER::ShaderParameterOverride& value : overrides) {
        const auto* definition = FindParameterDefinition(binding, value.key);
        ASSETS::SHADER::ShaderParameterSlot packed{};
        if (definition == nullptr ||
            !ASSETS::SHADER::PackShaderParameterValue(*definition, value.value, packed, outError)) {
            if (outError.empty()) {
                outError = "SpriteMaterial instance override does not match the Shader metadata: " + value.key;
            }
            return false;
        }
        slots[definition->slot] = packed;
    }
    outSlots = slots;
    return true;
}

} // namespace

void WorldSpriteComponent::SerializeMaterialState(nlohmann::json& out) const {
    out = nlohmann::json{{"enabled", materialEnabled_},
                         {"assetGuid", materialAssetGuid_.value},
                         {"parameters", SPRITE::WriteSpriteMaterialParameterValues(materialOverrides_)}};
}

void WorldSpriteComponent::DeserializeMaterialState(const nlohmann::json& node) {
    materialBinding_.reset();
    materialAssetGuid_ = {};
    materialOverrides_.clear();
    effectiveMaterialParameterSlots_ = {};
    materialEnabled_ = false;
    materialDiagnostic_.clear();
    if (node.is_null()) {
        return;
    }
    if (!node.is_object() || node.size() != 3u || !node.contains("enabled") || !node.contains("assetGuid") ||
        !node.contains("parameters")) {
        materialDiagnostic_ = "WorldSprite spriteMaterial must contain enabled, assetGuid, and parameters.";
        return;
    }

    bool enabled = false;
    std::string assetGuid{};
    std::vector<ASSETS::SHADER::ShaderParameterOverride> overrides{};
    std::string error{};
    if (!JSONSTRICT::ReadBool(node, "enabled", false, enabled, error, "spriteMaterial") ||
        !JSONSTRICT::ReadString(node, "assetGuid", {}, assetGuid, error, "spriteMaterial") ||
        (!assetGuid.empty() && !IsValidAssetGuid(assetGuid)) ||
        !SPRITE::ParseSpriteMaterialParameterValues(node.at("parameters"), overrides, error,
                                                    "spriteMaterial.parameters") ||
        (assetGuid.empty() && (enabled || !overrides.empty()))) {
        materialDiagnostic_ = error.empty() ? "WorldSprite spriteMaterial asset GUID is invalid." : std::move(error);
        return;
    }
    materialAssetGuid_ = AssetGuid{std::move(assetGuid)};
    materialEnabled_ = enabled;
    materialOverrides_ = std::move(overrides);
}

bool WorldSpriteComponent::BindMaterialAsset(const AssetGuid& guid,
                                             std::shared_ptr<const SPRITE::SpriteMaterialBinding> binding) {
    if (!guid.IsValid() || !binding || binding->materialAssetGuid != guid || !binding->asset ||
        !binding->shaderArtifact) {
        return false;
    }
    ASSETS::SHADER::ShaderParameterSlots slots{};
    std::string error{};
    if (!BuildEffectiveSlots(*binding, {}, slots, error)) {
        materialDiagnostic_ = std::move(error);
        return false;
    }
    if (materialAssetGuid_ == guid && materialBinding_ == binding && materialEnabled_ && materialOverrides_.empty()) {
        materialDiagnostic_.clear();
        return true;
    }
    materialAssetGuid_ = guid;
    materialBinding_ = std::move(binding);
    materialOverrides_.clear();
    effectiveMaterialParameterSlots_ = slots;
    materialEnabled_ = true;
    materialDiagnostic_.clear();
    NotifyMaterialStateDirty();
    return true;
}

bool WorldSpriteComponent::RefreshMaterialBinding(std::shared_ptr<const SPRITE::SpriteMaterialBinding> binding) {
    if (!binding || !materialAssetGuid_.IsValid() || binding->materialAssetGuid != materialAssetGuid_ ||
        !binding->asset || !binding->shaderArtifact) {
        return false;
    }
    std::vector<ASSETS::SHADER::ShaderParameterOverride> canonicalOverrides{};
    canonicalOverrides.reserve(materialOverrides_.size());
    for (const ASSETS::SHADER::ShaderParameterOverride& value : materialOverrides_) {
        const auto* definition = FindParameterDefinition(*binding, value.key);
        BINDING::Value canonical{};
        std::string error{};
        if (definition == nullptr ||
            !ASSETS::SHADER::ValidateShaderParameterValue(*definition, value.value, canonical, error)) {
            materialDiagnostic_ = "SpriteMaterial refresh rejected an incompatible instance override: " + value.key;
            return false;
        }
        canonicalOverrides.push_back({value.key, canonical});
    }
    ASSETS::SHADER::ShaderParameterSlots slots{};
    std::string slotError{};
    if (!BuildEffectiveSlots(*binding, canonicalOverrides, slots, slotError)) {
        materialDiagnostic_ = std::move(slotError);
        return false;
    }
    const bool sameOverrides =
        canonicalOverrides.size() == materialOverrides_.size() &&
        std::equal(canonicalOverrides.begin(), canonicalOverrides.end(), materialOverrides_.begin(),
                   [](const auto& lhs, const auto& rhs) { return lhs.key == rhs.key && lhs.value == rhs.value; });
    if (materialBinding_ == binding && sameOverrides) {
        materialDiagnostic_.clear();
        return true;
    }
    materialBinding_ = std::move(binding);
    materialOverrides_ = std::move(canonicalOverrides);
    effectiveMaterialParameterSlots_ = slots;
    materialDiagnostic_.clear();
    NotifyMaterialStateDirty();
    return true;
}

void WorldSpriteComponent::ClearMaterialAsset() {
    if (!materialAssetGuid_.IsValid() && !materialBinding_ && !materialEnabled_ && materialOverrides_.empty()) {
        materialDiagnostic_.clear();
        return;
    }
    materialAssetGuid_ = {};
    materialBinding_.reset();
    materialOverrides_.clear();
    effectiveMaterialParameterSlots_ = {};
    materialEnabled_ = false;
    materialDiagnostic_.clear();
    NotifyMaterialStateDirty();
}

bool WorldSpriteComponent::SetMaterialEnabled(bool enabled) noexcept {
    if (!materialAssetGuid_.IsValid()) {
        return false;
    }
    if (materialEnabled_ == enabled) {
        return true;
    }
    materialEnabled_ = enabled;
    NotifyMaterialStateDirty();
    return true;
}

size_t WorldSpriteComponent::GetMaterialParameterCount() const noexcept {
    return materialBinding_ && materialBinding_->shaderArtifact ? materialBinding_->shaderArtifact->parameters.size()
                                                                : 0u;
}

bool WorldSpriteComponent::TryGetMaterialParameterInfo(size_t index,
                                                       ASSETS::SHADER::ShaderParameterDefinition& outInfo) const {
    if (!materialBinding_ || !materialBinding_->shaderArtifact ||
        index >= materialBinding_->shaderArtifact->parameters.size()) {
        return false;
    }
    outInfo = materialBinding_->shaderArtifact->parameters[index];
    return true;
}

bool WorldSpriteComponent::HasMaterialParameterOverride(std::string_view key) const noexcept {
    return std::any_of(materialOverrides_.begin(), materialOverrides_.end(),
                       [key](const auto& value) { return value.key == key; });
}

bool WorldSpriteComponent::SetMaterialParameter(std::string_view key, const BINDING::Value& value) {
    if (!materialBinding_) {
        return false;
    }
    const auto* definition = FindParameterDefinition(*materialBinding_, key);
    BINDING::Value canonical{};
    std::string error{};
    if (definition == nullptr || !ASSETS::SHADER::ValidateShaderParameterValue(*definition, value, canonical, error)) {
        return false;
    }
    ASSETS::SHADER::ShaderParameterSlot packed{};
    if (!ASSETS::SHADER::PackShaderParameterValue(*definition, canonical, packed, error)) {
        return false;
    }
    const auto existing = std::find_if(materialOverrides_.begin(), materialOverrides_.end(),
                                       [key](const auto& parameter) { return parameter.key == key; });
    if (existing != materialOverrides_.end() && existing->value == canonical) {
        return true;
    }
    if (existing != materialOverrides_.end()) {
        existing->value = canonical;
    } else {
        materialOverrides_.push_back({std::string(key), canonical});
        std::sort(materialOverrides_.begin(), materialOverrides_.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.key < rhs.key; });
    }
    effectiveMaterialParameterSlots_[definition->slot] = packed;
    NotifyMaterialStateDirty();
    return true;
}

bool WorldSpriteComponent::TryGetMaterialParameter(std::string_view key, BINDING::Value& outValue) const noexcept {
    if (!materialBinding_ || !materialBinding_->asset || !materialBinding_->shaderArtifact) {
        return false;
    }
    const auto* definition = FindParameterDefinition(*materialBinding_, key);
    if (definition == nullptr) {
        return false;
    }
    const auto instanceOverride = std::find_if(materialOverrides_.begin(), materialOverrides_.end(),
                                               [key](const auto& value) { return value.key == key; });
    if (instanceOverride != materialOverrides_.end()) {
        outValue = instanceOverride->value;
        return true;
    }
    const auto materialValue =
        std::find_if(materialBinding_->asset->parameters.begin(), materialBinding_->asset->parameters.end(),
                     [key](const auto& value) { return value.key == key; });
    outValue =
        materialValue != materialBinding_->asset->parameters.end() ? materialValue->value : definition->defaultValue;
    return true;
}

bool WorldSpriteComponent::ResetMaterialParameter(std::string_view key) {
    if (!materialBinding_ || FindParameterDefinition(*materialBinding_, key) == nullptr) {
        return false;
    }
    const auto existing = std::find_if(materialOverrides_.begin(), materialOverrides_.end(),
                                       [key](const auto& value) { return value.key == key; });
    if (existing == materialOverrides_.end()) {
        return true;
    }
    std::vector<ASSETS::SHADER::ShaderParameterOverride> candidateOverrides = materialOverrides_;
    const auto candidateExisting = std::find_if(candidateOverrides.begin(), candidateOverrides.end(),
                                                [key](const auto& value) { return value.key == key; });
    candidateOverrides.erase(candidateExisting);
    ASSETS::SHADER::ShaderParameterSlots candidateSlots{};
    std::string error{};
    if (!BuildEffectiveSlots(*materialBinding_, candidateOverrides, candidateSlots, error)) {
        materialDiagnostic_ = std::move(error);
        return false;
    }
    materialOverrides_ = std::move(candidateOverrides);
    effectiveMaterialParameterSlots_ = candidateSlots;
    NotifyMaterialStateDirty();
    return true;
}

bool WorldSpriteComponent::ResetMaterialParameters() {
    if (materialOverrides_.empty()) {
        return true;
    }
    materialOverrides_.clear();
    if (materialBinding_) {
        effectiveMaterialParameterSlots_ = materialBinding_->parameterSlots;
    } else {
        effectiveMaterialParameterSlots_ = {};
    }
    NotifyMaterialStateDirty();
    return true;
}

void WorldSpriteComponent::NotifyMaterialStateDirty() noexcept {
    BumpMaterialStateRevision();
    NotifyRenderStateDirty();
}

void WorldSpriteComponent::BumpMaterialStateRevision() noexcept {
    ++materialStateRevision_;
    if (materialStateRevision_ == 0u) {
        materialStateRevision_ = 1u;
    }
}

void WorldSpriteComponent::BuildMaterialInspector(IInspectorBuilder& builder) {
    if (!builder.Section("Sprite material", false)) {
        return;
    }
    const auto* services = builder.GetContext().worldServices;
    const auto* store = services != nullptr ? services->Find<SPRITE::SpriteMaterialStore>() : nullptr;
    if (materialAssetGuid_.IsValid() && !materialBinding_ && store != nullptr) {
        std::string error{};
        auto binding = store->Resolve(materialAssetGuid_, error);
        if (binding != nullptr) {
            (void)RefreshMaterialBinding(std::move(binding));
        } else {
            materialDiagnostic_ = std::move(error);
        }
    }
    std::string candidate = materialAssetGuid_.value;
    if (builder.AssetIdPicker({"spriteMaterial.assetGuid", "Sprite material", "Sprite shader material preset."},
                              AssetType::SpriteMaterial, candidate)) {
        if (candidate.empty()) {
            ClearMaterialAsset();
        } else {
            std::string error{};
            const auto binding = store != nullptr ? store->Resolve(AssetGuid{candidate}, error) : nullptr;
            if (binding != nullptr && BindMaterialAsset(AssetGuid{candidate}, binding)) {
                candidate = materialAssetGuid_.value;
            } else {
                materialDiagnostic_ = error.empty() ? "SpriteMaterial could not be resolved in this World." : error;
            }
        }
    }

    if (!materialAssetGuid_.IsValid()) {
        return;
    }
    if (!materialDiagnostic_.empty()) {
        builder.Text(materialDiagnostic_);
    }
    if (!materialBinding_) {
        builder.Text("Sprite material parameters are unavailable. Check the material and Shader import diagnostics.");
        return;
    }

    bool candidateEnabled = materialEnabled_;
    if (builder.Bool({"spriteMaterial.enabled", "Use sprite material", "Select the bound custom Sprite pixel shader."},
                     candidateEnabled)) {
        (void)SetMaterialEnabled(candidateEnabled);
    }
    if (!materialBinding_->shaderArtifact) {
        return;
    }

    std::vector<std::pair<std::string, bool>> groups{};
    for (const ASSETS::SHADER::ShaderParameterDefinition& definition : materialBinding_->shaderArtifact->parameters) {
        const std::string group = definition.group.empty() ? "Shader parameters" : definition.group;
        auto groupState =
            std::find_if(groups.begin(), groups.end(), [&group](const auto& item) { return item.first == group; });
        if (groupState == groups.end()) {
            groups.emplace_back(group, builder.Section(group, true));
            groupState = std::prev(groups.end());
        }
        if (!groupState->second) {
            continue;
        }
        BINDING::Value value{};
        if (!TryGetMaterialParameter(definition.key, value)) {
            continue;
        }
        const std::string fieldId = "spriteMaterial.parameters." + definition.key;
        const AuthoringFieldDescriptor field{fieldId, definition.label, "Instance override of Shader material value."};
        if (builder.TypedValue(field, value, definition.hasRange, definition.minimum, definition.maximum,
                               definition.step) &&
            !SetMaterialParameter(definition.key, value)) {
            builder.Text("Shader parameter value was rejected by its declared type or range.");
        }
        builder.Text(HasMaterialParameterOverride(definition.key) ? "Instance override"
                                                                  : "Inherited material/Shader value");
        if (HasMaterialParameterOverride(definition.key) &&
            builder.Button("Reset override: " + definition.label + "###spriteMaterial.reset." + definition.key)) {
            (void)ResetMaterialParameter(definition.key);
        }
    }
}

} // namespace HIKARI
