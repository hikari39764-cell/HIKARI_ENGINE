#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Authoring/HIKARI_ComponentAuthoringTypes.h"

namespace HIKARI {

    namespace BINDING {
        enum class ValueType : uint8_t;
    }

    namespace INPUT {
        enum class InputActionValueType;
    }

    enum class AssetType;
    class AssetDatabase;
    class AssetRegistry;
    class IModelMaterialSlotProvider;
    class WorldServiceRegistry;
    struct RuntimeObjectHandle;
    struct SceneDocument;
    struct SceneObjectId;

    struct InspectorContext {
        AssetRegistry* assetRegistry = nullptr;
        AssetDatabase* assetDatabase = nullptr;
        const SceneDocument* sceneDocument = nullptr;
        const WorldServiceRegistry* worldServices = nullptr;
        const RuntimeObjectHandle* runtimeObject = nullptr;
        IModelMaterialSlotProvider* modelMaterialSlotProvider = nullptr;
    };

    class IInspectorBuilder {
    public:
        virtual ~IInspectorBuilder() = default;

        virtual void SetContext(const InspectorContext& context) = 0;
        virtual const InspectorContext& GetContext() const noexcept = 0;

        virtual void Text(std::string_view text) = 0;
        virtual bool Section(
            std::string_view label,
            bool defaultOpen = false) {

            (void)defaultOpen;
            Text(label);
            return true;
        }
        virtual bool Button(std::string_view label) = 0;
        virtual bool Bool(std::string_view label, bool& value) = 0;
        virtual bool Bool(
            const AuthoringFieldDescriptor& field,
            bool& value) {

            return Bool(field.label, value);
        }
        virtual bool Int(std::string_view label, int& value) = 0;
        virtual bool Int(
            const AuthoringFieldDescriptor& field,
            int& value) {

            return Int(field.label, value);
        }
        virtual bool Float(std::string_view label, float& value) = 0;
        virtual bool Float(
            const AuthoringFieldDescriptor& field,
            float& value) {

            return Float(field.label, value);
        }
        virtual bool FloatRange(
            std::string_view label,
            float& value,
            float minimum,
            float maximum,
            float speed = 0.1f) = 0;
        virtual bool FloatRange(
            const AuthoringFieldDescriptor& field,
            float& value,
            float minimum,
            float maximum,
            float speed = 0.1f) {

            return FloatRange(
                field.label,
                value,
                minimum,
                maximum,
                speed);
        }
        virtual bool Color3(
            std::string_view label,
            float& red,
            float& green,
            float& blue) {

            bool changed = false;
            const std::string prefix(label);
            changed = FloatRange(
                prefix + " Red",
                red,
                0.0f,
                1.0f,
                0.01f) || changed;
            changed = FloatRange(
                prefix + " Green",
                green,
                0.0f,
                1.0f,
                0.01f) || changed;
            changed = FloatRange(
                prefix + " Blue",
                blue,
                0.0f,
                1.0f,
                0.01f) || changed;
            return changed;
        }
        virtual bool Color3(
            const AuthoringFieldDescriptor& field,
            float& red,
            float& green,
            float& blue) {

            return Color3(field.label, red, green, blue);
        }
        virtual bool String(std::string_view label, std::string& value) = 0;
        virtual bool String(
            const AuthoringFieldDescriptor& field,
            std::string& value) {

            return String(field.label, value);
        }
        virtual bool InputActionIdPicker(
            std::string_view label,
            INPUT::InputActionValueType expectedType,
            std::string& value) = 0;
        virtual bool InputActionIdPicker(
            const AuthoringFieldDescriptor& field,
            INPUT::InputActionValueType expectedType,
            std::string& value) {

            return InputActionIdPicker(
                field.label,
                expectedType,
                value);
        }
        virtual bool RuntimeVariableIdPicker(
            std::string_view label,
            BINDING::ValueType expectedType,
            std::string& value) = 0;
        virtual bool RuntimeVariableIdPicker(
            const AuthoringFieldDescriptor& field,
            BINDING::ValueType expectedType,
            std::string& value) {

            return RuntimeVariableIdPicker(
                field.label,
                expectedType,
                value);
        }
        virtual bool RuntimeEventIdPicker(
            std::string_view label,
            bool hasPayload,
            BINDING::ValueType payloadType,
            std::string& value) = 0;
        virtual bool RuntimeEventIdPicker(
            const AuthoringFieldDescriptor& field,
            bool hasPayload,
            BINDING::ValueType payloadType,
            std::string& value) {

            return RuntimeEventIdPicker(
                field.label,
                hasPayload,
                payloadType,
                value);
        }
        virtual bool RuntimeEventIdPicker(
            std::string_view label,
            AuthoringEventPayloadPolicy policy,
            BINDING::ValueType payloadType,
            std::string& value) {

            return RuntimeEventIdPicker(
                label,
                policy == AuthoringEventPayloadPolicy::Exact,
                payloadType,
                value);
        }
        virtual bool RuntimeEventIdPicker(
            const AuthoringFieldDescriptor& field,
            AuthoringEventPayloadPolicy policy,
            BINDING::ValueType payloadType,
            std::string& value) {

            return RuntimeEventIdPicker(
                field.label,
                policy,
                payloadType,
                value);
        }
        virtual bool Choice(
            std::string_view label,
            int& selectedIndex,
            std::span<const char* const> choices) = 0;
        virtual bool Choice(
            const AuthoringFieldDescriptor& field,
            int& selectedIndex,
            std::span<const AuthoringChoice> choices) {

            std::vector<std::string> labels{};
            labels.reserve(choices.size());
            for (const AuthoringChoice& choice : choices) {
                labels.emplace_back(choice.label);
            }
            std::vector<const char*> labelPointers{};
            labelPointers.reserve(labels.size());
            for (const std::string& label : labels) {
                labelPointers.push_back(label.c_str());
            }
            return Choice(field.label, selectedIndex, labelPointers);
        }
        virtual bool Vec2(std::string_view label, float& x, float& y) = 0;
        virtual bool Vec2(
            const AuthoringFieldDescriptor& field,
            float& x,
            float& y) {

            return Vec2(field.label, x, y);
        }
        virtual bool AssetIdPicker(
            std::string_view label,
            AssetType assetType,
            std::string& value) = 0;
        virtual bool AssetIdPicker(
            const AuthoringFieldDescriptor& field,
            AssetType assetType,
            std::string& value) {

            return AssetIdPicker(field.label, assetType, value);
        }
        virtual bool SceneIdPicker(
            std::string_view label,
            std::string& value) = 0;
        virtual bool SceneIdPicker(
            const AuthoringFieldDescriptor& field,
            std::string& value) {

            return SceneIdPicker(field.label, value);
        }
        virtual bool SceneObjectIdPicker(
            std::string_view label,
            SceneObjectId& value) = 0;
        virtual bool SceneObjectIdPicker(
            const AuthoringFieldDescriptor& field,
            SceneObjectId& value) {

            return SceneObjectIdPicker(field.label, value);
        }
    };

} // namespace HIKARI
