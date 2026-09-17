#include "HIKARI/Project/Inspector.h"

#include <algorithm>
#include <filesystem>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingTypes.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectFixedText.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {
    using namespace PROJECT::DETAIL;
    namespace {
    BINDING::ValueType ToRuntimeBindingType(
        GameProjectBindingValueType type) noexcept {

        switch (type) {
        case GameProjectBindingValueType::Bool:
            return BINDING::ValueType::Bool;
        case GameProjectBindingValueType::Int:
            return BINDING::ValueType::Int;
        case GameProjectBindingValueType::Float2:
            return BINDING::ValueType::Float2;
        case GameProjectBindingValueType::Float3:
            return BINDING::ValueType::Float3;
        case GameProjectBindingValueType::Float4:
            return BINDING::ValueType::Float4;
        case GameProjectBindingValueType::Color:
            return BINDING::ValueType::Color;
        case GameProjectBindingValueType::Float:
        default:
            return BINDING::ValueType::Float;
        }
    }

    } // namespace

    GameProjectInspector GameProjectSdkAccess::MakeInspector(
        IInspectorBuilder& builder) noexcept {
        return GameProjectInspector(&builder);
    }

    void GameProjectInspector::Text(
        std::string_view text) {
        if (auto* builder =
                static_cast<IInspectorBuilder*>(builder_)) {
            builder->Text(text);
        }
    }

    bool GameProjectInspector::Section(
        std::string_view label,
        bool defaultOpen) {
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr &&
            builder->Section(label, defaultOpen);
    }

    bool GameProjectInspector::Bool(
        std::string_view propertyId, std::string_view label, bool& value) {
        auto* builder = static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr && !propertyId.empty() &&
            builder->Bool(AuthoringFieldDescriptor{propertyId, label}, value);
    }

    bool GameProjectInspector::Int(
        std::string_view propertyId, std::string_view label, int& value) {
        auto* builder = static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr && !propertyId.empty() &&
            builder->Int(AuthoringFieldDescriptor{propertyId, label}, value);
    }

    bool GameProjectInspector::Float(
        std::string_view propertyId, std::string_view label, float& value) {
        auto* builder = static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr && !propertyId.empty() &&
            builder->Float(AuthoringFieldDescriptor{propertyId, label}, value);
    }

    bool GameProjectInspector::FloatRange(
        std::string_view propertyId, std::string_view label, float& value,
        float minimum, float maximum, float speed) {
        auto* builder = static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr && !propertyId.empty() &&
            builder->FloatRange(AuthoringFieldDescriptor{propertyId, label},
                value, minimum, maximum, speed);
    }

    bool GameProjectInspector::Color3(
        std::string_view propertyId, std::string_view label,
        float& red, float& green, float& blue) {
        auto* builder = static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr && !propertyId.empty() &&
            builder->Color3(AuthoringFieldDescriptor{propertyId, label}, red, green, blue);
    }

    bool GameProjectInspector::SceneObjectId(
        std::string_view propertyId, std::string_view label, uint64_t& value) {
        auto* builder = static_cast<IInspectorBuilder*>(builder_);
        if (builder == nullptr || propertyId.empty()) return false;
        HIKARI::SceneObjectId editable{value};
        const bool changed = builder->SceneObjectIdPicker(
            AuthoringFieldDescriptor{propertyId, label}, editable);
        value = editable.value;
        return changed;
    }

    bool GameProjectInspector::Bool(
        std::string_view label,
        bool& value) {
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr &&
            builder->Bool(label, value);
    }

    bool GameProjectInspector::Int(
        std::string_view label,
        int& value) {
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr &&
            builder->Int(label, value);
    }

    bool GameProjectInspector::Float(
        std::string_view label,
        float& value) {
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr &&
            builder->Float(label, value);
    }

    bool GameProjectInspector::FloatRange(
        std::string_view label,
        float& value,
        float minimum,
        float maximum,
        float speed) {
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr &&
            builder->FloatRange(
                label,
                value,
                minimum,
                maximum,
                speed);
    }

    bool GameProjectInspector::Color3(
        std::string_view label,
        float& red,
        float& green,
        float& blue) {
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr &&
            builder->Color3(label, red, green, blue);
    }

    bool GameProjectInspector::StringBuffer(
        std::string_view label,
        char* value,
        size_t capacity) {
        if (value == nullptr || capacity == 0u) {
            return false;
        }
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        if (builder == nullptr) {
            return false;
        }
        std::string editable(value);
        if (!builder->String(label, editable)) {
            return false;
        }
        const size_t count =
            (std::min)(editable.size(), capacity - 1u);
        std::memcpy(value, editable.data(), count);
        value[count] = '\0';
        return true;
    }

    bool GameProjectInspector::SceneAssetGuid(
        std::string_view propertyId,
        std::string_view label,
        char* value,
        size_t capacity) {
        if (propertyId.empty() || value == nullptr || capacity == 0u) {
            return false;
        }
        auto* builder = static_cast<IInspectorBuilder*>(builder_);
        std::string editable{};
        if (builder == nullptr ||
            !ReadStringBuffer(value, capacity, editable)) {
            return false;
        }
        const std::string original = editable;
        const AuthoringFieldDescriptor field{
            propertyId,
            label.empty() ? propertyId : label,
            "Scene asset GUID resolved by the runtime Scene Flow service."
        };
        const bool changed = builder->SceneIdPicker(field, editable);
        return CopyStringToReference(editable, value, capacity) &&
            (changed || original != editable);
    }

    bool GameProjectInspector::SequenceAssetGuid(
        std::string_view propertyId,
        std::string_view label,
        char* value,
        size_t capacity) {
        if (propertyId.empty() || value == nullptr || capacity == 0u) {
            return false;
        }
        auto* builder = static_cast<IInspectorBuilder*>(builder_);
        std::string editable{};
        if (builder == nullptr ||
            !ReadStringBuffer(value, capacity, editable)) {
            return false;
        }
        const std::string original = editable;
        const AuthoringFieldDescriptor field{
            propertyId,
            label.empty() ? propertyId : label,
            "Sequence asset GUID played by the runtime Sequence service."
        };
        const bool changed = builder->AssetIdPicker(
            field,
            AssetType::Sequence,
            editable);
        return CopyStringToReference(editable, value, capacity) &&
            (changed || original != editable);
    }

    bool GameProjectInspector::InputActionId(
        std::string_view propertyId,
        std::string_view label,
        GameProjectInputValueType expectedType,
        char* value,
        size_t capacity) {
        if (propertyId.empty() || value == nullptr || capacity == 0u) {
            return false;
        }
        INPUT::InputActionValueType internalType{};
        switch (expectedType) {
        case GameProjectInputValueType::Button:
            internalType = INPUT::InputActionValueType::Button;
            break;
        case GameProjectInputValueType::Axis1D:
            internalType = INPUT::InputActionValueType::Axis1D;
            break;
        case GameProjectInputValueType::Axis2D:
            internalType = INPUT::InputActionValueType::Axis2D;
            break;
        default:
            return false;
        }
        auto* builder = static_cast<IInspectorBuilder*>(builder_);
        std::string editable{};
        if (builder == nullptr ||
            !ReadStringBuffer(value, capacity, editable)) {
            return false;
        }
        const std::string original = editable;
        const AuthoringFieldDescriptor field{
            propertyId,
            label.empty() ? propertyId : label,
            "Input action id with a validated value type."
        };
        const bool changed = builder->InputActionIdPicker(
            field,
            internalType,
            editable);
        return CopyStringToReference(editable, value, capacity) &&
            (changed || original != editable);
    }

    bool GameProjectInspector::ModelMaterialSetAssetGuid(
        std::string_view propertyId,
        std::string_view label,
        char* value,
        size_t capacity) {
        if (propertyId.empty() || value == nullptr || capacity == 0u) {
            return false;
        }
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        if (builder == nullptr) {
            return false;
        }

        std::string editable{};
        if (!ReadStringBuffer(value, capacity, editable)) {
            return false;
        }
        const std::string original = editable;
        const AuthoringFieldDescriptor field{
            propertyId,
            label.empty() ? propertyId : label,
            "Formal Material Set asset GUID for a model presentation."
        };
        const bool pickerChanged = builder->AssetIdPicker(
            field,
            AssetType::ModelMaterialBinding,
            editable);
        if (!CopyStringToReference(editable, value, capacity)) {
            return false;
        }
        return pickerChanged || original != editable;
    }

    bool GameProjectInspector::Vec2(
        std::string_view label,
        float& x,
        float& y) {
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        return builder != nullptr &&
            builder->Vec2(label, x, y);
    }

    bool GameProjectInspector::VariableBinding(
        std::string_view label,
        GameProjectBindingValueType expectedType,
        char* value,
        size_t capacity) {
        if (value == nullptr || capacity == 0u) {
            return false;
        }
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        if (builder == nullptr) {
            return false;
        }
        std::string editable(value);
        if (!builder->RuntimeVariableIdPicker(
                label,
                ToRuntimeBindingType(expectedType),
                editable)) {
            return false;
        }
        const size_t count =
            (std::min)(editable.size(), capacity - 1u);
        std::memcpy(value, editable.data(), count);
        value[count] = '\0';
        return true;
    }

    bool GameProjectInspector::EventBinding(
        std::string_view label,
        bool hasPayload,
        GameProjectBindingValueType payloadType,
        char* value,
        size_t capacity) {
        if (value == nullptr || capacity == 0u) {
            return false;
        }
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        if (builder == nullptr) {
            return false;
        }
        std::string editable(value);
        if (!builder->RuntimeEventIdPicker(
                label,
                hasPayload,
                ToRuntimeBindingType(payloadType),
                editable)) {
            return false;
        }
        const size_t count =
            (std::min)(editable.size(), capacity - 1u);
        std::memcpy(value, editable.data(), count);
        value[count] = '\0';
        return true;
    }

    bool GameProjectInspector::VfxAssetReference(
        std::string_view label,
        GameProjectVfxAssetReference& reference) {
        return VfxAssetReference(label, label, reference);
    }

    bool GameProjectInspector::VfxAssetReference(
        std::string_view propertyId,
        std::string_view label,
        GameProjectVfxAssetReference& reference) {
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        if (builder == nullptr || propertyId.empty()) {
            return false;
        }

        const std::string previousGuid =
            ReadFixedString(reference.assetGuid);
        const std::string previousPath =
            ReadFixedString(reference.assetPath);
        const GameProjectVfxAssetReference original = reference;
        if ((reference.assetGuid[0] != '\0' && previousGuid.empty()) ||
            (reference.assetPath[0] != '\0' && previousPath.empty())) {
            return false;
        }

        const InspectorContext& context = builder->GetContext();
        const AssetRecord* record = nullptr;
        if (context.assetDatabase != nullptr &&
            reference.assetGuid[0] == '\0' &&
            reference.assetPath[0] != '\0') {
            record = context.assetDatabase->FindByPath(
                std::filesystem::path(
                    ReadFixedString(reference.assetPath)));
            if (record != nullptr &&
                record->type == AssetType::VfxEffect) {
                if (!CopyStringToReference(
                    record->guid.value,
                    reference.assetGuid,
                    sizeof(reference.assetGuid))) {
                    reference = original;
                    return false;
                }
            }
        }

        std::string guid = ReadFixedString(reference.assetGuid);
        const AuthoringFieldDescriptor field{
            propertyId,
            label.empty() ? propertyId : label,
            "VFX effect asset GUID with a synchronized source path fallback."
        };
        const bool pickerChanged = builder->AssetIdPicker(
            field,
            AssetType::VfxEffect,
            guid);
        if (!CopyStringToReference(
            guid,
            reference.assetGuid,
            sizeof(reference.assetGuid))) {
            reference = original;
            return false;
        }

        if (context.assetDatabase != nullptr &&
            reference.assetGuid[0] != '\0') {
            record = context.assetDatabase->FindByGuid(
                AssetGuid{ guid });
            if (record != nullptr &&
                record->type == AssetType::VfxEffect) {
                if (!CopyStringToReference(
                    record->sourcePath.generic_string(),
                    reference.assetPath,
                    sizeof(reference.assetPath))) {
                    reference = original;
                    return false;
                }
            } else if (previousGuid !=
                ReadFixedString(reference.assetGuid)) {
                // A changed GUID must not retain the source path belonging
                // to the previous asset when the database cannot resolve the
                // new value (or resolves a non-VFX record).
                reference.assetPath[0] = '\0';
            }
        } else if (reference.assetGuid[0] == '\0') {
            reference.assetPath[0] = '\0';
        }

        return pickerChanged ||
            previousGuid != ReadFixedString(reference.assetGuid) ||
            previousPath != ReadFixedString(reference.assetPath);
    }

    bool GameProjectInspector::TextureAssetReference(
        std::string_view label,
        GameProjectUiTextureReference& reference) {
        return TextureAssetReference(label, label, reference);
    }

    bool GameProjectInspector::TextureAssetReference(
        std::string_view propertyId,
        std::string_view label,
        GameProjectUiTextureReference& reference) {
        auto* builder =
            static_cast<IInspectorBuilder*>(builder_);
        if (builder == nullptr || propertyId.empty()) {
            return false;
        }

        const std::string previousGuid =
            ReadFixedString(reference.assetGuid);
        const std::string previousPath =
            ReadFixedString(reference.assetPath);
        const GameProjectUiTextureReference original = reference;
        if ((reference.assetGuid[0] != '\0' && previousGuid.empty()) ||
            (reference.assetPath[0] != '\0' && previousPath.empty())) {
            return false;
        }

        const InspectorContext& context = builder->GetContext();
        const AssetRecord* record = nullptr;
        if (context.assetDatabase != nullptr &&
            reference.assetGuid[0] == '\0' &&
            reference.assetPath[0] != '\0') {
            record = context.assetDatabase->FindByPath(
                std::filesystem::path(
                    ReadFixedString(reference.assetPath)));
            if (record != nullptr && record->type == AssetType::Texture) {
                if (!CopyStringToReference(
                        record->guid.value,
                        reference.assetGuid,
                        sizeof(reference.assetGuid))) {
                    reference = original;
                    return false;
                }
            }
        }

        std::string guid = ReadFixedString(reference.assetGuid);
        const AuthoringFieldDescriptor field{
            propertyId,
            label.empty() ? propertyId : label,
            "Texture asset GUID with a synchronized source path fallback."
        };
        const bool pickerChanged = builder->AssetIdPicker(
            field,
            AssetType::Texture,
            guid);
        if (!CopyStringToReference(
                guid,
                reference.assetGuid,
                sizeof(reference.assetGuid))) {
            reference = original;
            return false;
        }

        if (context.assetDatabase != nullptr &&
            reference.assetGuid[0] != '\0') {
            record = context.assetDatabase->FindByGuid(AssetGuid{ guid });
            if (record != nullptr && record->type == AssetType::Texture) {
                if (!CopyStringToReference(
                        record->sourcePath.generic_string(),
                        reference.assetPath,
                        sizeof(reference.assetPath))) {
                    reference = original;
                    return false;
                }
            }
            else if (previousGuid !=
                    ReadFixedString(reference.assetGuid)) {
                reference.assetPath[0] = '\0';
            }
        }
        else if (reference.assetGuid[0] == '\0') {
            reference.assetPath[0] = '\0';
        }

        return pickerChanged ||
            previousGuid != ReadFixedString(reference.assetGuid) ||
            previousPath != ReadFixedString(reference.assetPath);
    }

} // namespace HIKARI
