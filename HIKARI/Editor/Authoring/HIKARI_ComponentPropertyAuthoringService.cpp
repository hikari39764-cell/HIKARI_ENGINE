#include "Editor/Authoring/HIKARI_ComponentPropertyAuthoringService.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"

namespace HIKARI::EDITOR {

    namespace {

        const char* FieldTypeName(
            AuthoringFieldValueType type) noexcept {
            switch (type) {
            case AuthoringFieldValueType::Bool: return "bool";
            case AuthoringFieldValueType::Int: return "int";
            case AuthoringFieldValueType::Float: return "float";
            case AuthoringFieldValueType::String: return "string";
            case AuthoringFieldValueType::Color3: return "color3";
            case AuthoringFieldValueType::Vec2: return "vec2";
            case AuthoringFieldValueType::Choice: return "choice";
            case AuthoringFieldValueType::AssetId: return "assetId";
            case AuthoringFieldValueType::SceneId: return "sceneId";
            case AuthoringFieldValueType::SceneObjectId:
                return "sceneObjectId";
            case AuthoringFieldValueType::InputActionId:
                return "inputActionId";
            case AuthoringFieldValueType::RuntimeVariableId:
                return "runtimeVariableId";
            case AuthoringFieldValueType::RuntimeEventId:
                return "runtimeEventId";
            }
            return "unknown";
        }

        const char* AssetTypeName(AssetType type) noexcept {
            switch (type) {
            case AssetType::Model: return "Model";
            case AssetType::Scene: return "Scene";
            case AssetType::Sky: return "Sky";
            case AssetType::Texture: return "Texture";
            case AssetType::Material: return "Material";
            case AssetType::Animation: return "Animation";
            case AssetType::Particle: return "Particle";
            case AssetType::VfxEffect: return "VfxEffect";
            case AssetType::Sequence: return "Sequence";
            case AssetType::CombatAction: return "Combat Action";
            case AssetType::AnimationStateMachine:
                return "AnimationStateMachine";
            case AssetType::Dialogue: return "Dialogue";
            case AssetType::ModelMaterialBinding:
                return "ModelMaterialBinding";
            case AssetType::Unknown: return "Unknown";
            }
            return "Unknown";
        }

        const char* CommandParameterTypeName(
            AuthoringCommandParameterType type) noexcept {
            switch (type) {
            case AuthoringCommandParameterType::String: return "string";
            case AuthoringCommandParameterType::Float: return "number";
            case AuthoringCommandParameterType::Int: return "integer";
            case AuthoringCommandParameterType::Bool: return "boolean";
            case AuthoringCommandParameterType::Json: return "json";
            }
            return "unknown";
        }

        bool MatchesCommandParameterType(
            const nlohmann::json& value,
            AuthoringCommandParameterType type) noexcept {
            switch (type) {
            case AuthoringCommandParameterType::String:
                return value.is_string();
            case AuthoringCommandParameterType::Float:
                return value.is_number();
            case AuthoringCommandParameterType::Int:
                return value.is_number_integer() ||
                    value.is_number_unsigned();
            case AuthoringCommandParameterType::Bool:
                return value.is_boolean();
            case AuthoringCommandParameterType::Json:
                return true;
            }
            return false;
        }

        ComponentAuthoringServiceResult MakeFailure(
            std::string code,
            std::string message) {

            ComponentAuthoringServiceResult result{};
            result.code = std::move(code);
            result.message = std::move(message);
            return result;
        }

        class HeadlessComponentAuthoringBuilder final :
            public IInspectorBuilder {
        public:
            enum class Mode { Describe, Apply };

            explicit HeadlessComponentAuthoringBuilder(Mode mode)
                : mode_(mode) {}

            HeadlessComponentAuthoringBuilder(
                std::string propertyId,
                nlohmann::json value)
                : mode_(Mode::Apply),
                  requestedPropertyId_(std::move(propertyId)),
                  requestedValue_(std::move(value)) {}

            void SetContext(const InspectorContext& context) override {
                context_ = context;
            }

            const InspectorContext& GetContext() const noexcept override {
                return context_;
            }

            void Text(std::string_view) override {}
            bool Section(std::string_view, bool) override { return true; }
            bool Button(std::string_view) override { return false; }
            bool Bool(std::string_view, bool&) override { return false; }
            bool Int(std::string_view, int&) override { return false; }
            bool Float(std::string_view, float&) override { return false; }
            bool FloatRange(
                std::string_view,
                float&,
                float,
                float,
                float) override { return false; }
            bool Color3(
                std::string_view,
                float&,
                float&,
                float&) override { return false; }
            bool String(
                std::string_view,
                std::string&) override { return false; }
            bool InputActionIdPicker(
                std::string_view,
                INPUT::InputActionValueType,
                std::string&) override { return false; }
            bool RuntimeVariableIdPicker(
                std::string_view,
                BINDING::ValueType,
                std::string&) override { return false; }
            bool RuntimeEventIdPicker(
                std::string_view,
                bool,
                BINDING::ValueType,
                std::string&) override { return false; }
            bool Choice(
                std::string_view,
                int&,
                std::span<const char* const>) override { return false; }
            bool Vec2(
                std::string_view,
                float&,
                float&) override { return false; }
            bool AssetIdPicker(
                std::string_view,
                AssetType,
                std::string&) override { return false; }
            bool SceneIdPicker(
                std::string_view,
                std::string&) override { return false; }
            bool SceneObjectIdPicker(
                std::string_view,
                SceneObjectId&) override { return false; }

            bool Bool(
                const AuthoringFieldDescriptor& field,
                bool& value) override {

                const auto apply = [this, &value]() {
                    if (!requestedValue_.is_boolean()) {
                        SetTypeError("boolean");
                        return false;
                    }
                    const bool next = requestedValue_.get<bool>();
                    const bool changed = next != value;
                    value = next;
                    return changed;
                };
                return Visit(
                    field,
                    AuthoringFieldValueType::Bool,
                    value,
                    nlohmann::json::object(),
                    apply);
            }

            bool Int(
                const AuthoringFieldDescriptor& field,
                int& value) override {

                const auto apply = [this, &value]() {
                    if (!requestedValue_.is_number_integer() &&
                        !requestedValue_.is_number_unsigned()) {
                        SetTypeError("integer");
                        return false;
                    }
                    const int64_t next = requestedValue_.is_number_unsigned()
                        ? requestedValue_.get<uint64_t>() <=
                                static_cast<uint64_t>(
                                    (std::numeric_limits<int>::max)())
                            ? static_cast<int64_t>(
                                requestedValue_.get<uint64_t>())
                            : (std::numeric_limits<int64_t>::max)()
                        : requestedValue_.get<int64_t>();
                    if (next < (std::numeric_limits<int>::min)() ||
                        next > (std::numeric_limits<int>::max)()) {
                        SetError(
                            "VALUE_OUT_OF_RANGE",
                            "Integer value is outside the supported range.");
                        return false;
                    }
                    const int converted = static_cast<int>(next);
                    const bool changed = converted != value;
                    value = converted;
                    return changed;
                };
                return Visit(
                    field,
                    AuthoringFieldValueType::Int,
                    value,
                    nlohmann::json::object(),
                    apply);
            }

            bool Float(
                const AuthoringFieldDescriptor& field,
                float& value) override {

                return VisitFloat(field, value, std::nullopt);
            }

            bool FloatRange(
                const AuthoringFieldDescriptor& field,
                float& value,
                float minimum,
                float maximum,
                float speed) override {

                return VisitFloat(
                    field,
                    value,
                    FloatMetadata{ minimum, maximum, speed });
            }

            bool Color3(
                const AuthoringFieldDescriptor& field,
                float& red,
                float& green,
                float& blue) override {

                const nlohmann::json current = { red, green, blue };
                const auto apply =
                    [this, &red, &green, &blue]() {
                    if (!IsNumberArray(requestedValue_, 3u)) {
                        SetTypeError("array of 3 numbers");
                        return false;
                    }
                    const float nextRed = requestedValue_[0].get<float>();
                    const float nextGreen = requestedValue_[1].get<float>();
                    const float nextBlue = requestedValue_[2].get<float>();
                    if (!IsUnitValue(nextRed) ||
                        !IsUnitValue(nextGreen) ||
                        !IsUnitValue(nextBlue)) {
                        SetError(
                            "VALUE_OUT_OF_RANGE",
                            "Color channels must be between 0 and 1.");
                        return false;
                    }
                    const bool changed = red != nextRed ||
                        green != nextGreen || blue != nextBlue;
                    red = nextRed;
                    green = nextGreen;
                    blue = nextBlue;
                    return changed;
                };
                return Visit(
                    field,
                    AuthoringFieldValueType::Color3,
                    current,
                    { { "minimum", 0.0f }, { "maximum", 1.0f } },
                    apply);
            }

            bool String(
                const AuthoringFieldDescriptor& field,
                std::string& value) override {

                return VisitString(
                    field,
                    AuthoringFieldValueType::String,
                    value,
                    nlohmann::json::object(),
                    nullptr);
            }

            bool InputActionIdPicker(
                const AuthoringFieldDescriptor& field,
                INPUT::InputActionValueType expectedType,
                std::string& value) override {

                return VisitString(
                    field,
                    AuthoringFieldValueType::InputActionId,
                    value,
                    { { "inputValueType", static_cast<int>(expectedType) } },
                    nullptr);
            }

            bool RuntimeVariableIdPicker(
                const AuthoringFieldDescriptor& field,
                BINDING::ValueType expectedType,
                std::string& value) override {
                const auto validator = [this, expectedType](
                    const std::string& next) {
                    if (next.empty()) {
                        return true;
                    }
                    const auto* store =
                        context_.worldServices != nullptr
                            ? context_.worldServices->Find<
                                BINDING::RuntimeBindingStore>()
                            : nullptr;
                    const auto* definition = store != nullptr
                        ? store->GetCatalog().FindVariable(next)
                        : nullptr;
                    if (definition == nullptr) {
                        SetError(
                            "RUNTIME_VARIABLE_NOT_FOUND",
                            "Runtime variable was not found: " + next);
                        return false;
                    }
                    if (definition->defaultValue.type != expectedType) {
                        SetError(
                            "RUNTIME_VARIABLE_TYPE_MISMATCH",
                            "Runtime variable type is incompatible.");
                        return false;
                    }
                    return true;
                };
                return VisitString(
                    field,
                    AuthoringFieldValueType::RuntimeVariableId,
                    value,
                    { { "valueType", BINDING::ToString(expectedType) } },
                    validator);
            }

            bool RuntimeEventIdPicker(
                const AuthoringFieldDescriptor& field,
                bool hasPayload,
                BINDING::ValueType payloadType,
                std::string& value) override {
                const auto validator =
                    [this, hasPayload, payloadType](
                        const std::string& next) {
                    if (next.empty()) {
                        return true;
                    }
                    const auto* store =
                        context_.worldServices != nullptr
                            ? context_.worldServices->Find<
                                BINDING::RuntimeBindingStore>()
                            : nullptr;
                    const auto* definition = store != nullptr
                        ? store->GetCatalog().FindEvent(next)
                        : nullptr;
                    if (definition == nullptr) {
                        SetError(
                            "RUNTIME_EVENT_NOT_FOUND",
                            "Runtime event was not found: " + next);
                        return false;
                    }
                    if (definition->hasPayload != hasPayload ||
                        (hasPayload &&
                         definition->payloadType != payloadType)) {
                        SetError(
                            "RUNTIME_EVENT_TYPE_MISMATCH",
                            "Runtime event payload is incompatible.");
                        return false;
                    }
                    return true;
                };
                return VisitString(
                    field,
                    AuthoringFieldValueType::RuntimeEventId,
                    value,
                    {
                        { "hasPayload", hasPayload },
                        { "payloadType", BINDING::ToString(payloadType) }
                    },
                    validator);
            }

            bool RuntimeEventIdPicker(
                const AuthoringFieldDescriptor& field,
                AuthoringEventPayloadPolicy policy,
                BINDING::ValueType payloadType,
                std::string& value) override {

                const auto validator =
                    [this, policy, payloadType](
                        const std::string& next) {
                    if (next.empty()) {
                        return true;
                    }
                    const auto* store =
                        context_.worldServices != nullptr
                            ? context_.worldServices->Find<
                                BINDING::RuntimeBindingStore>()
                            : nullptr;
                    const auto* definition = store != nullptr
                        ? store->GetCatalog().FindEvent(next)
                        : nullptr;
                    if (definition == nullptr) {
                        SetError(
                            "RUNTIME_EVENT_NOT_FOUND",
                            "Runtime event was not found: " + next);
                        return false;
                    }
                    const bool compatible =
                        policy == AuthoringEventPayloadPolicy::Any ||
                        (policy == AuthoringEventPayloadPolicy::None &&
                         !definition->hasPayload) ||
                        (policy == AuthoringEventPayloadPolicy::Exact &&
                         definition->hasPayload &&
                         definition->payloadType == payloadType);
                    if (!compatible) {
                        SetError(
                            "RUNTIME_EVENT_TYPE_MISMATCH",
                            "Runtime event payload is incompatible.");
                    }
                    return compatible;
                };

                const char* policyName = "Any";
                if (policy == AuthoringEventPayloadPolicy::None) {
                    policyName = "None";
                } else if (
                    policy == AuthoringEventPayloadPolicy::Exact) {
                    policyName = "Exact";
                }
                return VisitString(
                    field,
                    AuthoringFieldValueType::RuntimeEventId,
                    value,
                    {
                        { "payloadPolicy", policyName },
                        { "payloadType", BINDING::ToString(payloadType) }
                    },
                    validator);
            }

            bool Choice(
                const AuthoringFieldDescriptor& field,
                int& selectedIndex,
                std::span<const AuthoringChoice> choices) override {

                nlohmann::json choiceData = nlohmann::json::array();
                for (const AuthoringChoice& choice : choices) {
                    choiceData.push_back({
                        { "id", choice.id },
                        { "label", choice.label }
                    });
                }
                const std::string current =
                    selectedIndex >= 0 &&
                    static_cast<size_t>(selectedIndex) < choices.size()
                        ? std::string(choices[
                            static_cast<size_t>(selectedIndex)].id)
                        : std::string{};
                const auto apply =
                    [this, &selectedIndex, choices]() {
                    if (!requestedValue_.is_string()) {
                        SetTypeError("string choice id");
                        return false;
                    }
                    const std::string requested =
                        requestedValue_.get<std::string>();
                    for (size_t index = 0u;
                        index < choices.size();
                        ++index) {
                        if (choices[index].id == requested) {
                            const int next = static_cast<int>(index);
                            const bool changed = next != selectedIndex;
                            selectedIndex = next;
                            return changed;
                        }
                    }
                    SetError(
                        "CHOICE_NOT_FOUND",
                        "Unknown choice id: " + requested);
                    return false;
                };
                return Visit(
                    field,
                    AuthoringFieldValueType::Choice,
                    current,
                    { { "choices", std::move(choiceData) } },
                    apply);
            }

            bool Vec2(
                const AuthoringFieldDescriptor& field,
                float& x,
                float& y) override {

                const nlohmann::json current = { x, y };
                const auto apply = [this, &x, &y]() {
                    if (!IsNumberArray(requestedValue_, 2u)) {
                        SetTypeError("array of 2 numbers");
                        return false;
                    }
                    const float nextX = requestedValue_[0].get<float>();
                    const float nextY = requestedValue_[1].get<float>();
                    const bool changed = x != nextX || y != nextY;
                    x = nextX;
                    y = nextY;
                    return changed;
                };
                return Visit(
                    field,
                    AuthoringFieldValueType::Vec2,
                    current,
                    nlohmann::json::object(),
                    apply);
            }

            bool AssetIdPicker(
                const AuthoringFieldDescriptor& field,
                AssetType assetType,
                std::string& value) override {

                const auto validator = [this, assetType](
                    const std::string& next) {
                    if (next.empty() || context_.assetDatabase == nullptr) {
                        return true;
                    }
                    const AssetRecord* record =
                        context_.assetDatabase->FindByGuid(
                            AssetGuid{ next });
                    if (record == nullptr) {
                        SetError(
                            "ASSET_NOT_FOUND",
                            "Asset GUID was not found: " + next);
                        return false;
                    }
                    if (record->type != assetType) {
                        SetError(
                            "ASSET_TYPE_MISMATCH",
                            "Asset must be " +
                                std::string(AssetTypeName(assetType)) +
                                ", received " +
                                AssetTypeName(record->type) + ".");
                        return false;
                    }
                    return true;
                };
                return VisitString(
                    field,
                    AuthoringFieldValueType::AssetId,
                    value,
                    { { "assetType", AssetTypeName(assetType) } },
                    validator);
            }

            bool SceneIdPicker(
                const AuthoringFieldDescriptor& field,
                std::string& value) override {

                return VisitString(
                    field,
                    AuthoringFieldValueType::SceneId,
                    value,
                    nlohmann::json::object(),
                    nullptr);
            }

            bool SceneObjectIdPicker(
                const AuthoringFieldDescriptor& field,
                SceneObjectId& value) override {

                const auto apply = [this, &value]() {
                    if (!requestedValue_.is_number_unsigned() &&
                        !requestedValue_.is_number_integer()) {
                        SetTypeError("unsigned integer object id");
                        return false;
                    }
                    if (!requestedValue_.is_number_unsigned() &&
                        requestedValue_.get<int64_t>() < 0) {
                        SetTypeError("unsigned integer object id");
                        return false;
                    }
                    const SceneObjectId next{
                        requestedValue_.get<uint64_t>()
                    };
                    if (next.value != 0u &&
                        context_.sceneDocument != nullptr &&
                        FindSceneObjectData(
                            *context_.sceneDocument,
                            next) == nullptr) {
                        SetError(
                            "OBJECT_NOT_FOUND",
                            "Scene object id was not found: " +
                                std::to_string(next.value));
                        return false;
                    }
                    const bool changed = !(next == value);
                    value = next;
                    return changed;
                };
                return Visit(
                    field,
                    AuthoringFieldValueType::SceneObjectId,
                    value.value,
                    nlohmann::json::object(),
                    apply);
            }

            bool HasError() const noexcept { return !code_.empty(); }
            bool FoundRequestedProperty() const noexcept {
                return foundRequestedProperty_;
            }
            bool Changed() const noexcept { return changed_; }
            const std::string& Code() const noexcept { return code_; }
            const std::string& Message() const noexcept { return message_; }
            const nlohmann::json& Fields() const noexcept { return fields_; }

        private:
            struct FloatMetadata {
                float minimum = 0.0f;
                float maximum = 0.0f;
                float speed = 0.1f;
            };

            template <class ApplyFn>
            bool Visit(
                const AuthoringFieldDescriptor& field,
                AuthoringFieldValueType type,
                nlohmann::json currentValue,
                nlohmann::json metadata,
                ApplyFn&& apply) {

                if (!RegisterField(field, type, currentValue, metadata)) {
                    return false;
                }
                if (mode_ != Mode::Apply ||
                    field.id != requestedPropertyId_ || HasError()) {
                    return false;
                }
                foundRequestedProperty_ = true;
                if (!field.writable) {
                    SetError(
                        "PROPERTY_READ_ONLY",
                        "Property is read-only: " +
                            requestedPropertyId_);
                    return false;
                }
                const bool changed = apply();
                changed_ = changed_ || changed;
                return changed;
            }

            bool VisitFloat(
                const AuthoringFieldDescriptor& field,
                float& value,
                std::optional<FloatMetadata> range) {

                nlohmann::json metadata = nlohmann::json::object();
                if (range.has_value()) {
                    metadata = {
                        { "minimum", range->minimum },
                        { "maximum", range->maximum },
                        { "step", range->speed }
                    };
                }
                const auto apply = [this, &value, range]() {
                    if (!requestedValue_.is_number()) {
                        SetTypeError("number");
                        return false;
                    }
                    const float next = requestedValue_.get<float>();
                    if (range.has_value() &&
                        (next < range->minimum || next > range->maximum)) {
                        SetError(
                            "VALUE_OUT_OF_RANGE",
                            "Number must be between " +
                                std::to_string(range->minimum) + " and " +
                                std::to_string(range->maximum) + ".");
                        return false;
                    }
                    const bool changed = next != value;
                    value = next;
                    return changed;
                };
                return Visit(
                    field,
                    AuthoringFieldValueType::Float,
                    value,
                    std::move(metadata),
                    apply);
            }

            template <class Validator>
            bool VisitString(
                const AuthoringFieldDescriptor& field,
                AuthoringFieldValueType type,
                std::string& value,
                nlohmann::json metadata,
                Validator validator) {

                const auto apply = [this, &value, &validator]() {
                    if (!requestedValue_.is_string()) {
                        SetTypeError("string");
                        return false;
                    }
                    const std::string next =
                        requestedValue_.get<std::string>();
                    if constexpr (!std::is_same_v<Validator,
                        std::nullptr_t>) {
                        if (!validator(next)) {
                            return false;
                        }
                    }
                    const bool changed = next != value;
                    value = next;
                    return changed;
                };
                return Visit(
                    field,
                    type,
                    value,
                    std::move(metadata),
                    apply);
            }

            bool RegisterField(
                const AuthoringFieldDescriptor& field,
                AuthoringFieldValueType type,
                const nlohmann::json& currentValue,
                const nlohmann::json& metadata) {

                if (field.id.empty()) {
                    SetError(
                        "EMPTY_PROPERTY_ID",
                        "Authoring fields must have a stable property id.");
                    return false;
                }
                if (!fieldIds_.insert(std::string(field.id)).second) {
                    SetError(
                        "DUPLICATE_PROPERTY_ID",
                        "Duplicate component property id: " +
                            std::string(field.id));
                    return false;
                }
                nlohmann::json entry{
                    { "id", std::string(field.id) },
                    { "label", std::string(field.label) },
                    { "description", std::string(field.description) },
                    { "type", FieldTypeName(type) },
                    { "writable", field.writable },
                    { "value", currentValue }
                };
                for (const auto& [name, value] : metadata.items()) {
                    entry[name] = value;
                }
                fields_.push_back(std::move(entry));
                return true;
            }

            static bool IsNumberArray(
                const nlohmann::json& value,
                size_t expectedSize) {

                if (!value.is_array() || value.size() != expectedSize) {
                    return false;
                }
                return std::all_of(
                    value.begin(),
                    value.end(),
                    [](const nlohmann::json& element) {
                        return element.is_number();
                    });
            }

            static bool IsUnitValue(float value) noexcept {
                return value >= 0.0f && value <= 1.0f;
            }

            void SetTypeError(std::string_view expected) {
                SetError(
                    "PROPERTY_TYPE_MISMATCH",
                    "Property '" + requestedPropertyId_ +
                        "' expects " + std::string(expected) + ".");
            }

            void SetError(std::string code, std::string message) {
                if (code_.empty()) {
                    code_ = std::move(code);
                    message_ = std::move(message);
                }
            }

            Mode mode_ = Mode::Describe;
            InspectorContext context_{};
            std::string requestedPropertyId_{};
            nlohmann::json requestedValue_{};
            nlohmann::json fields_ = nlohmann::json::array();
            std::unordered_set<std::string> fieldIds_{};
            std::string code_{};
            std::string message_{};
            bool foundRequestedProperty_ = false;
            bool changed_ = false;
        };

        ComponentAuthoringServiceResult ResolveTarget(
            const ComponentRegistry& registry,
            SceneDocument& document,
            ComponentAuthoringTarget target,
            SceneObjectData*& outObject,
            SceneComponentData*& outComponent,
            const ComponentTypeInfo*& outTypeInfo) {

            outObject = FindSceneObjectData(document, target.objectId);
            if (outObject == nullptr) {
                return MakeFailure(
                    "OBJECT_NOT_FOUND",
                    "Scene object id was not found: " +
                        std::to_string(target.objectId.value));
            }
            outComponent = FindSceneComponent(
                *outObject,
                target.componentId);
            if (outComponent == nullptr) {
                return MakeFailure(
                    "COMPONENT_NOT_FOUND",
                    "Component id was not found on object " +
                        std::to_string(target.objectId.value) + ": " +
                        std::to_string(target.componentId.value));
            }
            outTypeInfo = registry.Find(outComponent->type);
            if (outTypeInfo == nullptr || !outTypeInfo->factory) {
                return MakeFailure(
                    "COMPONENT_TYPE_UNAVAILABLE",
                    "Component type is not registered: " +
                        outComponent->type);
            }
            ComponentAuthoringServiceResult result{};
            result.ok = true;
            return result;
        }

        ComponentAuthoringServiceResult ValidateCommandArguments(
            const ComponentAuthoringCommandDefinition& command,
            const nlohmann::json& arguments) {

            if (!arguments.is_object()) {
                return MakeFailure(
                    "COMMAND_ARGUMENTS_NOT_OBJECT",
                    "Component command arguments must be a JSON object.");
            }
            for (const AuthoringCommandParameter& parameter :
                command.parameters) {
                const auto found = arguments.find(parameter.name);
                if (found == arguments.end()) {
                    if (parameter.required) {
                        return MakeFailure(
                            "MISSING_COMMAND_PARAMETER",
                            "Missing component command parameter: " +
                                parameter.name);
                    }
                    continue;
                }
                if (!MatchesCommandParameterType(
                        *found,
                        parameter.type)) {
                    return MakeFailure(
                        "COMMAND_PARAMETER_TYPE_MISMATCH",
                        "Component command parameter '" +
                            parameter.name + "' must be " +
                            CommandParameterTypeName(parameter.type) + ".");
                }
            }
            for (const auto& [name, value] : arguments.items()) {
                (void)value;
                const auto known = std::find_if(
                    command.parameters.begin(),
                    command.parameters.end(),
                    [&name](const AuthoringCommandParameter& parameter) {
                        return parameter.name == name;
                    });
                if (known == command.parameters.end()) {
                    return MakeFailure(
                        "UNKNOWN_COMMAND_PARAMETER",
                        "Unknown component command parameter: " + name);
                }
            }
            ComponentAuthoringServiceResult result{};
            result.ok = true;
            return result;
        }

        ComponentAuthoringServiceResult ValidateAuthoringState(
            const ComponentTypeInfo& typeInfo,
            const IComponent& component,
            const InspectorContext& context) {

            ComponentAuthoringServiceResult result{};
            if (!typeInfo.validateAuthoring) {
                result.ok = true;
                return result;
            }
            try {
                const AuthoringCommandResult validation =
                    typeInfo.validateAuthoring(component, context);
                if (!validation.ok) {
                    return MakeFailure(
                        validation.code.empty()
                            ? "COMPONENT_AUTHORING_INVALID"
                            : validation.code,
                        validation.message.empty()
                            ? "Component authoring validation failed."
                            : validation.message);
                }
            } catch (const std::exception& error) {
                return MakeFailure(
                    "COMPONENT_AUTHORING_VALIDATION_EXCEPTION",
                    error.what());
            } catch (...) {
                return MakeFailure(
                    "COMPONENT_AUTHORING_VALIDATION_EXCEPTION",
                    "Component authoring validator threw an unknown exception.");
            }
            result.ok = true;
            return result;
        }

        ComponentAuthoringServiceResult DeserializeForAuthoring(
            IComponent& component,
            const nlohmann::json& properties,
            std::string_view componentType) {

            try {
                std::string diagnostic{};
                if (!component.TryDeserialize(properties, diagnostic)) {
                    return MakeFailure(
                        "COMPONENT_DESERIALIZATION_FAILED",
                        "Component authoring data is invalid for " +
                            std::string(componentType) +
                            (diagnostic.empty()
                                ? "."
                                : ": " + diagnostic));
                }
            } catch (const std::exception& error) {
                return MakeFailure(
                    "COMPONENT_DESERIALIZATION_EXCEPTION",
                    "Component deserialization threw for " +
                        std::string(componentType) + ": " +
                        error.what());
            } catch (...) {
                return MakeFailure(
                    "COMPONENT_DESERIALIZATION_EXCEPTION",
                    "Component deserialization threw an unknown exception for " +
                        std::string(componentType) + ".");
            }
            ComponentAuthoringServiceResult result{};
            result.ok = true;
            return result;
        }

        ComponentAuthoringServiceResult SerializeForAuthoring(
            const IComponent& component,
            nlohmann::json& outProperties,
            std::string_view componentType) {

            try {
                component.Serialize(outProperties);
            } catch (const std::exception& error) {
                return MakeFailure(
                    "COMPONENT_SERIALIZATION_EXCEPTION",
                    "Component serialization threw for " +
                        std::string(componentType) + ": " +
                        error.what());
            } catch (...) {
                return MakeFailure(
                    "COMPONENT_SERIALIZATION_EXCEPTION",
                    "Component serialization threw an unknown exception for " +
                        std::string(componentType) + ".");
            }
            ComponentAuthoringServiceResult result{};
            result.ok = true;
            return result;
        }

        ComponentAuthoringServiceResult ValidateStoredAuthoringState(
            const ComponentRegistry& registry,
            SceneDocument& document,
            ComponentAuthoringTarget target,
            InspectorContext context) {

            SceneObjectData* object = nullptr;
            SceneComponentData* component = nullptr;
            const ComponentTypeInfo* typeInfo = nullptr;
            ComponentAuthoringServiceResult resolved = ResolveTarget(
                registry,
                document,
                target,
                object,
                component,
                typeInfo);
            if (!resolved.ok) {
                return resolved;
            }
            std::unique_ptr<IComponent> instance = typeInfo->factory();
            if (!instance) {
                return MakeFailure(
                    "COMPONENT_FACTORY_FAILED",
                    "Component factory returned no instance: " +
                        component->type);
            }
            ComponentAuthoringServiceResult deserialized =
                DeserializeForAuthoring(
                    *instance,
                    component->properties,
                    component->type);
            if (!deserialized.ok) {
                return deserialized;
            }
            context.sceneDocument = &document;
            return ValidateAuthoringState(
                *typeInfo,
                *instance,
                context);
        }

        nlohmann::json SerializeCommands(
            const ComponentTypeInfo& typeInfo) {

            nlohmann::json commands = nlohmann::json::array();
            for (const ComponentAuthoringCommandDefinition& command :
                typeInfo.authoringCommands) {
                nlohmann::json parameters = nlohmann::json::array();
                for (const AuthoringCommandParameter& parameter :
                    command.parameters) {
                    parameters.push_back({
                        { "name", parameter.name },
                        {
                            "type",
                            CommandParameterTypeName(parameter.type)
                        },
                        { "required", parameter.required },
                        { "description", parameter.description }
                    });
                }
                commands.push_back({
                    { "id", command.id },
                    { "summary", command.summary },
                    { "parameters", std::move(parameters) }
                });
            }
            return commands;
        }

    } // namespace

    ComponentAuthoringServiceResult
    ComponentPropertyAuthoringService::Describe(
        const ComponentRegistry& registry,
        const SceneDocument& document,
        ComponentAuthoringTarget target,
        InspectorContext context) const {

        SceneDocument working = document;
        SceneObjectData* object = nullptr;
        SceneComponentData* component = nullptr;
        const ComponentTypeInfo* typeInfo = nullptr;
        ComponentAuthoringServiceResult resolved = ResolveTarget(
            registry,
            working,
            target,
            object,
            component,
            typeInfo);
        if (!resolved.ok) {
            return resolved;
        }

        std::unique_ptr<IComponent> instance = typeInfo->factory();
        if (!instance) {
            return MakeFailure(
                "COMPONENT_FACTORY_FAILED",
                "Component factory returned no instance: " +
                    component->type);
        }
        HeadlessComponentAuthoringBuilder builder(
            HeadlessComponentAuthoringBuilder::Mode::Describe);
        context.sceneDocument = &working;
        builder.SetContext(context);
        const ComponentAuthoringServiceResult deserialized =
            DeserializeForAuthoring(
                *instance,
                component->properties,
                component->type);
        if (!deserialized.ok) {
            return deserialized;
        }
        try {
            instance->BuildInspector(builder);
        } catch (const std::exception& error) {
            return MakeFailure(
                "COMPONENT_AUTHORING_EXCEPTION",
                error.what());
        } catch (...) {
            return MakeFailure(
                "COMPONENT_AUTHORING_EXCEPTION",
                "Component authoring schema threw an unknown exception.");
        }
        if (builder.HasError()) {
            return MakeFailure(builder.Code(), builder.Message());
        }

        ComponentAuthoringServiceResult result{};
        result.ok = true;
        result.message = "Component authoring schema returned.";
        result.data = {
            { "objectId", target.objectId.value },
            { "objectName", object->name },
            { "componentId", target.componentId.value },
            { "componentType", component->type },
            { "fields", builder.Fields() },
            { "commands", SerializeCommands(*typeInfo) }
        };
        return result;
    }

    ComponentAuthoringServiceResult
    ComponentPropertyAuthoringService::Apply(
        const ComponentRegistry& registry,
        SceneDocument& document,
        std::span<const ComponentPropertyMutation> mutations,
        InspectorContext context) const {

        if (mutations.empty()) {
            return MakeFailure(
                "EMPTY_TRANSACTION",
                "At least one component property mutation is required.");
        }

        SceneDocument working = document;
        context.sceneDocument = &working;
        nlohmann::json changedFields = nlohmann::json::array();
        std::vector<ComponentAuthoringTarget> changedTargets{};
        for (const ComponentPropertyMutation& mutation : mutations) {
            if (mutation.propertyId.empty()) {
                return MakeFailure(
                    "EMPTY_PROPERTY_ID",
                    "Component property id must not be empty.");
            }
            SceneObjectData* object = nullptr;
            SceneComponentData* component = nullptr;
            const ComponentTypeInfo* typeInfo = nullptr;
            ComponentAuthoringServiceResult resolved = ResolveTarget(
                registry,
                working,
                mutation.target,
                object,
                component,
                typeInfo);
            if (!resolved.ok) {
                return resolved;
            }

            std::unique_ptr<IComponent> instance = typeInfo->factory();
            if (!instance) {
                return MakeFailure(
                    "COMPONENT_FACTORY_FAILED",
                    "Component factory returned no instance: " +
                        component->type);
            }
            HeadlessComponentAuthoringBuilder builder(
                mutation.propertyId,
                mutation.value);
            builder.SetContext(context);
            const ComponentAuthoringServiceResult deserialized =
                DeserializeForAuthoring(
                    *instance,
                    component->properties,
                    component->type);
            if (!deserialized.ok) {
                return deserialized;
            }
            try {
                instance->BuildInspector(builder);
            } catch (const std::exception& error) {
                return MakeFailure(
                    "COMPONENT_AUTHORING_EXCEPTION",
                    error.what());
            } catch (...) {
                return MakeFailure(
                    "COMPONENT_AUTHORING_EXCEPTION",
                    "Component authoring property builder threw an unknown exception.");
            }
            if (builder.HasError()) {
                return MakeFailure(builder.Code(), builder.Message());
            }
            if (!builder.FoundRequestedProperty()) {
                return MakeFailure(
                    "PROPERTY_NOT_FOUND",
                    "Component property is unavailable in the current "
                    "component state: " + mutation.propertyId);
            }
            if (!builder.Changed()) {
                continue;
            }
            nlohmann::json serialized = nlohmann::json::object();
            const ComponentAuthoringServiceResult serializedResult =
                SerializeForAuthoring(
                    *instance,
                    serialized,
                    component->type);
            if (!serializedResult.ok) {
                return serializedResult;
            }
            component->properties = std::move(serialized);
            const auto knownTarget = std::find_if(
                changedTargets.begin(),
                changedTargets.end(),
                [&mutation](const ComponentAuthoringTarget& target) {
                    return target.objectId == mutation.target.objectId &&
                        target.componentId == mutation.target.componentId;
                });
            if (knownTarget == changedTargets.end()) {
                changedTargets.push_back(mutation.target);
            }
            changedFields.push_back({
                { "objectId", mutation.target.objectId.value },
                { "componentId", mutation.target.componentId.value },
                { "componentType", component->type },
                { "propertyId", mutation.propertyId }
            });
        }
        for (const ComponentAuthoringTarget& changedTarget :
            changedTargets) {
            const ComponentAuthoringServiceResult validation =
                ValidateStoredAuthoringState(
                    registry,
                    working,
                    changedTarget,
                    context);
            if (!validation.ok) {
                return validation;
            }
        }

        ComponentAuthoringServiceResult result{};
        result.ok = true;
        result.documentChanged = !changedFields.empty();
        result.code = result.documentChanged ? "OK" : "NO_CHANGE";
        result.message = result.documentChanged
            ? "Component property transaction applied."
            : "Component property transaction produced no change.";
        result.data = {
            { "changed", std::move(changedFields) }
        };
        if (result.documentChanged) {
            document.objects = std::move(working.objects);
        }
        return result;
    }

    ComponentAuthoringServiceResult
    ComponentPropertyAuthoringService::ExecuteCommand(
        const ComponentRegistry& registry,
        SceneDocument& document,
        ComponentAuthoringTarget target,
        std::string_view commandId,
        const nlohmann::json& arguments,
        InspectorContext context) const {

        SceneDocument working = document;
        context.sceneDocument = &working;
        SceneObjectData* object = nullptr;
        SceneComponentData* component = nullptr;
        const ComponentTypeInfo* typeInfo = nullptr;
        ComponentAuthoringServiceResult resolved = ResolveTarget(
            registry,
            working,
            target,
            object,
            component,
            typeInfo);
        if (!resolved.ok) {
            return resolved;
        }
        const auto command = std::find_if(
            typeInfo->authoringCommands.begin(),
            typeInfo->authoringCommands.end(),
            [commandId](
                const ComponentAuthoringCommandDefinition& candidate) {
                return candidate.id == commandId;
            });
        if (command == typeInfo->authoringCommands.end() ||
            !command->execute) {
            return MakeFailure(
                "COMPONENT_COMMAND_NOT_FOUND",
                "Component command is not registered: " +
                    std::string(commandId));
        }
        ComponentAuthoringServiceResult validation =
            ValidateCommandArguments(*command, arguments);
        if (!validation.ok) {
            return validation;
        }

        std::unique_ptr<IComponent> instance = typeInfo->factory();
        if (!instance) {
            return MakeFailure(
                "COMPONENT_FACTORY_FAILED",
                "Component factory returned no instance: " +
                    component->type);
        }
        const ComponentAuthoringServiceResult deserialized =
            DeserializeForAuthoring(
                *instance,
                component->properties,
                component->type);
        if (!deserialized.ok) {
            return deserialized;
        }
        AuthoringCommandResult commandResult{};
        try {
            commandResult = command->execute(*instance, arguments);
        } catch (const std::exception& error) {
            return MakeFailure(
                "COMPONENT_COMMAND_EXCEPTION",
                error.what());
        } catch (...) {
            return MakeFailure(
                "COMPONENT_COMMAND_EXCEPTION",
                "Component command threw an unknown exception.");
        }
        if (!commandResult.ok) {
            return MakeFailure(
                commandResult.code.empty()
                    ? "COMPONENT_COMMAND_FAILED"
                    : commandResult.code,
                commandResult.message);
        }
        const ComponentAuthoringServiceResult authoringValidation =
            ValidateAuthoringState(*typeInfo, *instance, context);
        if (!authoringValidation.ok) {
            return authoringValidation;
        }

        nlohmann::json serialized = nlohmann::json::object();
        const ComponentAuthoringServiceResult serializedResult =
            SerializeForAuthoring(
                *instance,
                serialized,
                component->type);
        if (!serializedResult.ok) {
            return serializedResult;
        }
        const bool changed = serialized != component->properties;
        const std::string componentType = component->type;
        if (changed) {
            component->properties = std::move(serialized);
            document.objects = std::move(working.objects);
        }

        ComponentAuthoringServiceResult result{};
        result.ok = true;
        result.documentChanged = changed;
        result.code = changed ? "OK" : "NO_CHANGE";
        result.message = commandResult.message.empty()
            ? (changed
                ? "Component command applied."
                : "Component command produced no change.")
            : commandResult.message;
        result.data = {
            { "objectId", target.objectId.value },
            { "componentId", target.componentId.value },
            { "componentType", componentType },
            { "commandId", std::string(commandId) },
            { "result", commandResult.data }
        };
        return result;
    }

} // namespace HIKARI::EDITOR
