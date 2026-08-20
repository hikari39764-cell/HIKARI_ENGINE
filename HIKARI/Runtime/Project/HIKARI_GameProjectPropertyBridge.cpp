#include "HIKARI/Project/Inspector.h"
#include "HIKARI/Project/PropertyArchive.h"

#include <algorithm>
#include <cstring>

#include <json.hpp>

#include "Authoring/HIKARI_IInspectorBuilder.h"
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

    std::string PropertyReader::ReadString(
        std::string_view name,
        std::string_view fallback) const {
        if (!Contains(name)) {
            return std::string(fallback);
        }
        const size_t size = GetStringSize(name);
        if (size == 0u) {
            return {};
        }
        std::string value(size, '\0');
        if (!CopyString(name, value.data(), value.size() + 1u)) {
            return std::string(fallback);
        }
        return value;
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

} // namespace HIKARI
