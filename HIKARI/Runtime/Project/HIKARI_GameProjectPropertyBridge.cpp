#include "HIKARI/Project/Inspector.h"
#include "HIKARI/Project/PropertyArchive.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Authoring/HIKARI_ComponentAuthoringTypes.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingTypes.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"

namespace HIKARI {
namespace {

    const nlohmann::json* FindProperty(
        const void* properties,
        std::string_view name) noexcept {

        const auto* object =
            static_cast<const nlohmann::json*>(properties);
        if (object == nullptr || !object->is_object()) {
            return nullptr;
        }
        const auto found = object->find(std::string(name));
        return found != object->end() ? &*found : nullptr;
    }

    nlohmann::json* GetPropertyObject(void* properties) {
        auto* object = static_cast<nlohmann::json*>(properties);
        if (object != nullptr && !object->is_object()) {
            *object = nlohmann::json::object();
        }
        return object;
    }

    template<size_t Capacity>
    std::string ReadFixedString(
        const char (&value)[Capacity]) {
        size_t count = 0u;
        while (count < Capacity && value[count] != '\0') {
            ++count;
        }
        if (count == Capacity) {
            return {};
        }
        return std::string(value, count);
    }

    template<size_t Capacity>
    bool CopyFixedString(
        const nlohmann::json* value,
        char (&destination)[Capacity]) noexcept {
        std::memset(destination, 0, Capacity);
        if (value == nullptr || !value->is_string()) {
            return false;
        }
        const std::string& source =
            value->get_ref<const std::string&>();
        if (source.size() >= Capacity) {
            return false;
        }
        const size_t count = source.size();
        std::memcpy(destination, source.data(), count);
        destination[count] = '\0';
        return true;
    }

    const nlohmann::json* FindObjectMember(
        const nlohmann::json& object,
        std::string_view primary,
        std::string_view legacy) noexcept {
        const auto found = object.find(std::string(primary));
        if (found != object.end()) {
            return &*found;
        }
        const auto legacyFound = object.find(std::string(legacy));
        return legacyFound != object.end() ? &*legacyFound : nullptr;
    }

    bool CopyStringToReference(
        std::string_view source,
        char* destination,
        size_t capacity) noexcept {
        if (destination == nullptr || capacity == 0u) {
            return false;
        }
        if (source.size() >= capacity) {
            return false;
        }
        const size_t count = source.size();
        std::memcpy(destination, source.data(), count);
        destination[count] = '\0';
        return true;
    }

    bool ReadStringBuffer(
        const char* source,
        size_t capacity,
        std::string& outValue) noexcept {
        if (source == nullptr || capacity == 0u) {
            return false;
        }
        size_t count = 0u;
        while (count < capacity && source[count] != '\0') {
            ++count;
        }
        if (count == capacity) {
            return false;
        }
        outValue.assign(source, count);
        return true;
    }

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

    PropertyReader GameProjectSdkAccess::MakePropertyReader(
        const void* properties) noexcept {
        return PropertyReader(properties);
    }

    PropertyWriter GameProjectSdkAccess::MakePropertyWriter(
        void* properties) noexcept {
        return PropertyWriter(properties);
    }

    GameProjectInspector GameProjectSdkAccess::MakeInspector(
        IInspectorBuilder& builder) noexcept {
        return GameProjectInspector(&builder);
    }

    bool PropertyReader::Contains(
        std::string_view name) const noexcept {
        return FindProperty(properties_, name) != nullptr;
    }

    bool PropertyReader::ReadBool(
        std::string_view name,
        bool fallback) const noexcept {
        const nlohmann::json* value =
            FindProperty(properties_, name);
        return value != nullptr && value->is_boolean()
            ? value->get<bool>()
            : fallback;
    }

    int64_t PropertyReader::ReadInt(
        std::string_view name,
        int64_t fallback) const noexcept {
        const nlohmann::json* value =
            FindProperty(properties_, name);
        if (value == nullptr || !value->is_number_integer()) {
            return fallback;
        }
        return value->get<int64_t>();
    }

    float PropertyReader::ReadFloat(
        std::string_view name,
        float fallback) const noexcept {
        const nlohmann::json* value =
            FindProperty(properties_, name);
        return value != nullptr && value->is_number()
            ? value->get<float>()
            : fallback;
    }

    GameProjectFloat3 PropertyReader::ReadFloat3(
        std::string_view name,
        GameProjectFloat3 fallback) const noexcept {
        const nlohmann::json* value =
            FindProperty(properties_, name);
        if (value == nullptr ||
            !value->is_array() ||
            value->size() < 3u ||
            !(*value)[0].is_number() ||
            !(*value)[1].is_number() ||
            !(*value)[2].is_number()) {
            return fallback;
        }
        return {
            (*value)[0].get<float>(),
            (*value)[1].get<float>(),
            (*value)[2].get<float>()
        };
    }

    bool PropertyReader::ReadVfxAssetReference(
        std::string_view name,
        GameProjectVfxAssetReference& outReference) const noexcept {
        outReference = {};
        try {
            const nlohmann::json* value = FindProperty(properties_, name);
            if (value == nullptr) {
                return false;
            }
            // Older project components may have stored only the GUID string.
            if (value->is_string()) {
                return CopyFixedString(
                    value,
                    outReference.assetGuid);
            }
            if (!value->is_object()) {
                return false;
            }
            const nlohmann::json* guid = FindObjectMember(
                *value,
                "assetGuid",
                "guid");
            const nlohmann::json* path = FindObjectMember(
                *value,
                "assetPath",
                "sourcePath");
            const bool guidFits = CopyFixedString(
                guid,
                outReference.assetGuid);
            const bool pathFits = CopyFixedString(
                path,
                outReference.assetPath);
            if ((guid != nullptr && !guidFits) ||
                (path != nullptr && !pathFits)) {
                outReference = {};
                return false;
            }
            return true;
        }
        catch (...) {
            outReference = {};
            return false;
        }
    }

    bool PropertyReader::ReadTextureAssetReference(
        std::string_view name,
        GameProjectUiTextureReference& outReference) const noexcept {
        outReference = {};
        try {
            const nlohmann::json* value = FindProperty(properties_, name);
            if (value == nullptr) {
                return false;
            }
            if (value->is_string()) {
                return CopyFixedString(value, outReference.assetGuid);
            }
            if (!value->is_object()) {
                return false;
            }
            const nlohmann::json* guid = FindObjectMember(
                *value, "assetGuid", "guid");
            const nlohmann::json* path = FindObjectMember(
                *value, "assetPath", "sourcePath");
            const bool guidFits = CopyFixedString(
                guid, outReference.assetGuid);
            const bool pathFits = CopyFixedString(
                path, outReference.assetPath);
            if ((guid != nullptr && !guidFits) ||
                (path != nullptr && !pathFits)) {
                outReference = {};
                return false;
            }
            return true;
        }
        catch (...) {
            outReference = {};
            return false;
        }
    }

    size_t PropertyReader::GetStringSize(
        std::string_view name) const noexcept {
        const nlohmann::json* value =
            FindProperty(properties_, name);
        return value != nullptr && value->is_string()
            ? value->get_ref<const std::string&>().size()
            : 0u;
    }

    bool PropertyReader::CopyString(
        std::string_view name,
        char* destination,
        size_t capacity) const noexcept {
        if (destination == nullptr || capacity == 0u) {
            return false;
        }
        const nlohmann::json* value =
            FindProperty(properties_, name);
        if (value == nullptr || !value->is_string()) {
            destination[0] = '\0';
            return false;
        }
        const std::string& source =
            value->get_ref<const std::string&>();
        const size_t count =
            (std::min)(source.size(), capacity - 1u);
        std::memcpy(destination, source.data(), count);
        destination[count] = '\0';
        return count == source.size();
    }

    void PropertyWriter::WriteBool(
        std::string_view name,
        bool value) {
        if (nlohmann::json* object =
                GetPropertyObject(properties_)) {
            (*object)[std::string(name)] = value;
        }
    }

    void PropertyWriter::WriteInt(
        std::string_view name,
        int64_t value) {
        if (nlohmann::json* object =
                GetPropertyObject(properties_)) {
            (*object)[std::string(name)] = value;
        }
    }

    void PropertyWriter::WriteFloat(
        std::string_view name,
        float value) {
        if (nlohmann::json* object =
                GetPropertyObject(properties_)) {
            (*object)[std::string(name)] = value;
        }
    }

    void PropertyWriter::WriteString(
        std::string_view name,
        std::string_view value) {
        if (nlohmann::json* object =
                GetPropertyObject(properties_)) {
            (*object)[std::string(name)] = value;
        }
    }

    void PropertyWriter::WriteFloat3(
        std::string_view name,
        GameProjectFloat3 value) {
        if (nlohmann::json* object =
                GetPropertyObject(properties_)) {
            (*object)[std::string(name)] = {
                value.x,
                value.y,
                value.z
            };
        }
    }

    void GameProjectInspector::Text(
        std::string_view text) {
        if (auto* builder =
                static_cast<IInspectorBuilder*>(builder_)) {
            builder->Text(text);
        }
    }

    void PropertyWriter::WriteVfxAssetReference(
        std::string_view name,
        const GameProjectVfxAssetReference& reference) {
        const std::string guid = ReadFixedString(reference.assetGuid);
        const std::string path = ReadFixedString(reference.assetPath);
        if ((reference.assetGuid[0] != '\0' && guid.empty()) ||
            (reference.assetPath[0] != '\0' && path.empty())) {
            return;
        }
        if (nlohmann::json* object =
                GetPropertyObject(properties_)) {
            (*object)[std::string(name)] = {
                { "assetGuid", guid },
                { "assetPath", path }
            };
        }
    }

    void PropertyWriter::WriteTextureAssetReference(
        std::string_view name,
        const GameProjectUiTextureReference& reference) {
        const std::string guid = ReadFixedString(reference.assetGuid);
        const std::string path = ReadFixedString(reference.assetPath);
        if ((reference.assetGuid[0] != '\0' && guid.empty()) ||
            (reference.assetPath[0] != '\0' && path.empty())) {
            return;
        }
        if (nlohmann::json* object =
                GetPropertyObject(properties_)) {
            (*object)[std::string(name)] = {
                { "assetGuid", guid },
                { "assetPath", path }
            };
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
