#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Editor/Authoring/HIKARI_ComponentPropertyAuthoringService.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "RenderContracts/Shader/HIKARI_ShaderParameterSerialization.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"

#include <cmath>
#include "Editor/Authoring/HIKARI_ComponentPropertyBuilder.h"

namespace HIKARI::EDITOR {
namespace {

const char* FieldTypeName(AuthoringFieldValueType type) noexcept {
    switch (type) {
    case AuthoringFieldValueType::Bool: return "bool";
    case AuthoringFieldValueType::Int: return "int";
    case AuthoringFieldValueType::Float: return "float";
    case AuthoringFieldValueType::String: return "string";
    case AuthoringFieldValueType::Color3: return "color3";
    case AuthoringFieldValueType::Vec2: return "vec2";
    case AuthoringFieldValueType::Vec3: return "vec3";
    case AuthoringFieldValueType::Choice: return "choice";
    case AuthoringFieldValueType::AssetId: return "assetId";
    case AuthoringFieldValueType::SceneId: return "sceneId";
    case AuthoringFieldValueType::SceneObjectId: return "sceneObjectId";
    case AuthoringFieldValueType::InputActionId: return "inputActionId";
    case AuthoringFieldValueType::RuntimeVariableId: return "runtimeVariableId";
    case AuthoringFieldValueType::RuntimeEventId: return "runtimeEventId";
    case AuthoringFieldValueType::TypedValue: return "typedValue";
    }
    return "unknown";
}

class HeadlessComponentAuthoringBuilder final : public IInspectorBuilder {
public:
    enum class Mode { Describe, Apply };

    explicit HeadlessComponentAuthoringBuilder(Mode mode) : mode_(mode) {}

    HeadlessComponentAuthoringBuilder(std::string propertyId, nlohmann::json value)
        : mode_(Mode::Apply), requestedPropertyId_(std::move(propertyId)), requestedValue_(std::move(value)) {}

    void SetContext(const InspectorContext& context) override { context_ = context; }

    const InspectorContext& GetContext() const noexcept override { return context_; }

    void Text(std::string_view) override {}
    bool Section(std::string_view, bool) override { return true; }
    bool Button(std::string_view) override { return false; }
    bool Bool(const AuthoringFieldDescriptor& field, bool& value) override {
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
        return Visit(field, AuthoringFieldValueType::Bool, value, nlohmann::json::object(), apply);
    }

    bool Int(const AuthoringFieldDescriptor& field, int& value) override {
        const auto apply = [this, &value]() {
            if (!requestedValue_.is_number_integer() && !requestedValue_.is_number_unsigned()) {
                SetTypeError("integer");
                return false;
            }
            const int64_t next =
                requestedValue_.is_number_unsigned()
                    ? requestedValue_.get<uint64_t>() <= static_cast<uint64_t>((std::numeric_limits<int>::max)())
                          ? static_cast<int64_t>(requestedValue_.get<uint64_t>())
                          : (std::numeric_limits<int64_t>::max)()
                    : requestedValue_.get<int64_t>();
            if (next < (std::numeric_limits<int>::min)() || next > (std::numeric_limits<int>::max)()) {
                SetError("VALUE_OUT_OF_RANGE", "Integer value is outside the supported range.");
                return false;
            }
            const int converted = static_cast<int>(next);
            const bool changed = converted != value;
            value = converted;
            return changed;
        };
        return Visit(field, AuthoringFieldValueType::Int, value, nlohmann::json::object(), apply);
    }

    bool Float(const AuthoringFieldDescriptor& field, float& value) override {
        return VisitFloat(field, value, std::nullopt);
    }

    bool FloatRange(const AuthoringFieldDescriptor& field, float& value, float minimum, float maximum,
                    float speed) override {
        return VisitFloat(field, value, FloatMetadata{minimum, maximum, speed});
    }

    bool TypedValue(const AuthoringFieldDescriptor& field, BINDING::Value& value, bool hasRange, float minimum,
                    float maximum, float step) override {
        const nlohmann::json current{{"type", BINDING::ToString(value.type)},
                                     {"value", ASSETS::SHADER::WriteShaderParameterValue(value)}};
        nlohmann::json metadata{{"valueType", BINDING::ToString(value.type)}, {"hasRange", hasRange}, {"step", step}};
        if (hasRange) {
            metadata["minimum"] = minimum;
            metadata["maximum"] = maximum;
        }
        const auto apply = [this, &value, hasRange, minimum, maximum]() {
            if (!requestedValue_.is_object() || requestedValue_.size() != 2u || !requestedValue_.contains("type") ||
                !requestedValue_.contains("value")) {
                SetTypeError("typed value object");
                return false;
            }
            std::string typeName{};
            std::string readError{};
            if (!JSONSTRICT::ReadString(requestedValue_, "type", {}, typeName, readError, "property") ||
                typeName != BINDING::ToString(value.type)) {
                SetError("PROPERTY_TYPE_MISMATCH", "Typed value type does not match its Shader metadata.");
                return false;
            }
            BINDING::Value next{};
            std::string error{};
            if (!ASSETS::SHADER::ReadShaderParameterValue(requestedValue_.at("value"), value.type, next, error) ||
                !BINDING::IsValueFinite(next)) {
                SetError("VALUE_OUT_OF_RANGE", error.empty() ? "Typed value must be finite." : error);
                return false;
            }
            if (hasRange && BINDING::IsNumeric(next.type) && !BINDING::IsValueWithinRange(next, minimum, maximum)) {
                SetError("VALUE_OUT_OF_RANGE", "Typed value is outside its Shader metadata range.");
                return false;
            }
            if (next.type == BINDING::ValueType::Int &&
                (next.integer < -ASSETS::SHADER::kShaderParameterMaximumExactInteger ||
                 next.integer > ASSETS::SHADER::kShaderParameterMaximumExactInteger)) {
                SetError("VALUE_OUT_OF_RANGE", "Integer value exceeds the exact Shader float range.");
                return false;
            }
            const bool changed = next != value;
            value = next;
            return changed;
        };
        return Visit(field, AuthoringFieldValueType::TypedValue, current, std::move(metadata), apply);
    }

    bool Color3(const AuthoringFieldDescriptor& field, float& red, float& green, float& blue) override {
        const nlohmann::json current = {red, green, blue};
        const auto apply = [this, &red, &green, &blue]() {
            if (!IsNumberArray(requestedValue_, 3u)) {
                SetTypeError("array of 3 numbers");
                return false;
            }
            const float nextRed = requestedValue_[0].get<float>();
            const float nextGreen = requestedValue_[1].get<float>();
            const float nextBlue = requestedValue_[2].get<float>();
            if (!IsUnitValue(nextRed) || !IsUnitValue(nextGreen) || !IsUnitValue(nextBlue)) {
                SetError("VALUE_OUT_OF_RANGE", "Color channels must be between 0 and 1.");
                return false;
            }
            const bool changed = red != nextRed || green != nextGreen || blue != nextBlue;
            red = nextRed;
            green = nextGreen;
            blue = nextBlue;
            return changed;
        };
        return Visit(field, AuthoringFieldValueType::Color3, current, {{"minimum", 0.0f}, {"maximum", 1.0f}}, apply);
    }

    bool String(const AuthoringFieldDescriptor& field, std::string& value) override {
        return VisitString(field, AuthoringFieldValueType::String, value, nlohmann::json::object(), nullptr);
    }

    bool InputActionIdPicker(const AuthoringFieldDescriptor& field, INPUT::InputActionValueType expectedType,
                             std::string& value) override {
        return VisitString(field, AuthoringFieldValueType::InputActionId, value,
                           {{"inputValueType", static_cast<int>(expectedType)}}, nullptr);
    }

    bool RuntimeVariableIdPicker(const AuthoringFieldDescriptor& field,
                                 std::span<const BINDING::ValueType> allowedTypes, std::string& value) override {
        if (allowedTypes.empty()) {
            SetError("RUNTIME_VARIABLE_TYPE_MISMATCH", "At least one runtime variable type is required.");
            return false;
        }
        const std::vector<BINDING::ValueType> acceptedTypes(allowedTypes.begin(), allowedTypes.end());
        const auto validator = [this, acceptedTypes](const std::string& next) {
            if (next.empty()) {
                return true;
            }
            const auto* store = context_.worldServices != nullptr
                                    ? context_.worldServices->Find<BINDING::RuntimeBindingStore>()
                                    : nullptr;
            const auto* definition = store != nullptr ? store->GetCatalog().FindVariable(next) : nullptr;
            if (definition == nullptr) {
                SetError("RUNTIME_VARIABLE_NOT_FOUND", "Runtime variable was not found: " + next);
                return false;
            }
            if (std::find(acceptedTypes.begin(), acceptedTypes.end(), definition->defaultValue.type) ==
                acceptedTypes.end()) {
                SetError("RUNTIME_VARIABLE_TYPE_MISMATCH", "Runtime variable type is incompatible.");
                return false;
            }
            return true;
        };
        nlohmann::json typeNames = nlohmann::json::array();
        for (const BINDING::ValueType type : acceptedTypes)
            typeNames.push_back(BINDING::ToString(type));
        nlohmann::json shape{{"valueTypes", std::move(typeNames)}};
        if (acceptedTypes.size() == 1u)
            shape["valueType"] = BINDING::ToString(acceptedTypes.front());
        return VisitString(field, AuthoringFieldValueType::RuntimeVariableId, value, shape, validator);
    }

    bool RuntimeEventIdPicker(const AuthoringFieldDescriptor& field, bool hasPayload, BINDING::ValueType payloadType,
                              std::string& value) override {
        const auto validator = [this, hasPayload, payloadType](const std::string& next) {
            return ValidateEventId(
                next, hasPayload ? AuthoringEventPayloadPolicy::Exact : AuthoringEventPayloadPolicy::None, payloadType);
        };
        return VisitString(field, AuthoringFieldValueType::RuntimeEventId, value,
                           {{"hasPayload", hasPayload}, {"payloadType", BINDING::ToString(payloadType)}}, validator);
    }

    bool RuntimeEventIdPicker(const AuthoringFieldDescriptor& field, AuthoringEventPayloadPolicy policy,
                              BINDING::ValueType payloadType, std::string& value) override {
        const auto validator = [this, policy, payloadType](const std::string& next) {
            return ValidateEventId(next, policy, payloadType);
        };

        const char* policyName = "Any";
        if (policy == AuthoringEventPayloadPolicy::None) {
            policyName = "None";
        } else if (policy == AuthoringEventPayloadPolicy::Exact) {
            policyName = "Exact";
        }
        return VisitString(field, AuthoringFieldValueType::RuntimeEventId, value,
                           {{"payloadPolicy", policyName}, {"payloadType", BINDING::ToString(payloadType)}}, validator);
    }

    bool Choice(const AuthoringFieldDescriptor& field, int& selectedIndex,
                std::span<const AuthoringChoice> choices) override {
        nlohmann::json choiceData = nlohmann::json::array();
        for (const AuthoringChoice& choice : choices) {
            choiceData.push_back({{"id", choice.id}, {"label", choice.label}});
        }
        const std::string current = selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < choices.size()
                                        ? std::string(choices[static_cast<size_t>(selectedIndex)].id)
                                        : std::string{};
        const auto apply = [this, &selectedIndex, choices]() {
            if (!requestedValue_.is_string()) {
                SetTypeError("string choice id");
                return false;
            }
            const std::string requested = requestedValue_.get<std::string>();
            for (size_t index = 0u; index < choices.size(); ++index) {
                if (choices[index].id == requested) {
                    const int next = static_cast<int>(index);
                    const bool changed = next != selectedIndex;
                    selectedIndex = next;
                    return changed;
                }
            }
            SetError("CHOICE_NOT_FOUND", "Unknown choice id: " + requested);
            return false;
        };
        return Visit(field, AuthoringFieldValueType::Choice, current, {{"choices", std::move(choiceData)}}, apply);
    }

    bool Vec2(const AuthoringFieldDescriptor& field, float& x, float& y) override {
        const nlohmann::json current = {x, y};
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
        return Visit(field, AuthoringFieldValueType::Vec2, current, nlohmann::json::object(), apply);
    }

    bool Vec3(const AuthoringFieldDescriptor& field, float& x, float& y, float& z) override {
        const nlohmann::json current = {x, y, z};
        const auto apply = [this, &x, &y, &z]() {
            if (!IsNumberArray(requestedValue_, 3u)) {
                SetTypeError("array of 3 numbers");
                return false;
            }
            const float nextX = requestedValue_[0].get<float>();
            const float nextY = requestedValue_[1].get<float>();
            const float nextZ = requestedValue_[2].get<float>();
            const bool changed = x != nextX || y != nextY || z != nextZ;
            x = nextX;
            y = nextY;
            z = nextZ;
            return changed;
        };
        return Visit(field, AuthoringFieldValueType::Vec3, current, nlohmann::json::object(), apply);
    }

    bool AssetIdPicker(const AuthoringFieldDescriptor& field, AssetType assetType, std::string& value) override {
        const auto validator = [this, assetType](const std::string& next) {
            if (next.empty() || context_.assetDatabase == nullptr) {
                return true;
            }
            const AssetRecord* record = context_.assetDatabase->FindByGuid(AssetGuid{next});
            if (record == nullptr) {
                SetError("ASSET_NOT_FOUND", "Asset GUID was not found: " + next);
                return false;
            }
            if (record->type != assetType) {
                SetError("ASSET_TYPE_MISMATCH", "Asset must be " + std::string(ASSETS::SEMANTICS::ToString(assetType)) +
                                                    ", received " +
                                                    std::string(ASSETS::SEMANTICS::ToString(record->type)) + ".");
                return false;
            }
            return true;
        };
        return VisitString(field, AuthoringFieldValueType::AssetId, value,
                           {{"assetType", std::string(ASSETS::SEMANTICS::ToString(assetType))}}, validator);
    }

    bool SceneIdPicker(const AuthoringFieldDescriptor& field, std::string& value) override {
        return VisitString(field, AuthoringFieldValueType::SceneId, value, nlohmann::json::object(), nullptr);
    }

    bool SceneObjectIdPicker(const AuthoringFieldDescriptor& field, SceneObjectId& value) override {
        const auto apply = [this, &value]() {
            if (!requestedValue_.is_number_unsigned() && !requestedValue_.is_number_integer()) {
                SetTypeError("unsigned integer object id");
                return false;
            }
            if (!requestedValue_.is_number_unsigned() && requestedValue_.get<int64_t>() < 0) {
                SetTypeError("unsigned integer object id");
                return false;
            }
            const SceneObjectId next{requestedValue_.get<uint64_t>()};
            if (next.value != 0u && context_.sceneDocument != nullptr &&
                FindSceneObjectData(*context_.sceneDocument, next) == nullptr) {
                SetError("OBJECT_NOT_FOUND", "Scene object id was not found: " + std::to_string(next.value));
                return false;
            }
            const bool changed = !(next == value);
            value = next;
            return changed;
        };
        return Visit(field, AuthoringFieldValueType::SceneObjectId, value.value, nlohmann::json::object(), apply);
    }

    bool HasError() const noexcept { return !code_.empty(); }
    bool FoundRequestedProperty() const noexcept { return foundRequestedProperty_; }
    bool Changed() const noexcept { return changed_; }
    const std::string& Code() const noexcept { return code_; }
    const std::string& Message() const noexcept { return message_; }
    const nlohmann::json& Fields() const noexcept { return fields_; }

private:
    bool ValidateEventId(const std::string& next, AuthoringEventPayloadPolicy policy, BINDING::ValueType payloadType) {
        if (next.empty()) {
            return true;
        }
        const auto* store =
            context_.worldServices != nullptr ? context_.worldServices->Find<BINDING::RuntimeBindingStore>() : nullptr;
        const auto* definition = store != nullptr ? store->GetCatalog().FindEvent(next) : nullptr;
        if (definition == nullptr) {
            SetError("RUNTIME_EVENT_NOT_FOUND", "Runtime event was not found: " + next);
            return false;
        }
        const bool compatible = policy == AuthoringEventPayloadPolicy::Any ||
                                (policy == AuthoringEventPayloadPolicy::None && !definition->hasPayload) ||
                                (policy == AuthoringEventPayloadPolicy::Exact && definition->hasPayload &&
                                 definition->payloadType == payloadType);
        if (!compatible) {
            SetError("RUNTIME_EVENT_TYPE_MISMATCH", "Runtime event payload is incompatible.");
        }
        return compatible;
    }

    struct FloatMetadata {
        float minimum = 0.0f;
        float maximum = 0.0f;
        float speed = 0.1f;
    };

    template <class ApplyFn>
    bool Visit(const AuthoringFieldDescriptor& field, AuthoringFieldValueType type, nlohmann::json currentValue,
               nlohmann::json metadata, ApplyFn&& apply) {
        if (!RegisterField(field, type, currentValue, metadata)) {
            return false;
        }
        if (mode_ != Mode::Apply || field.id != requestedPropertyId_ || HasError()) {
            return false;
        }
        foundRequestedProperty_ = true;
        if (!field.writable) {
            SetError("PROPERTY_READ_ONLY", "Property is read-only: " + requestedPropertyId_);
            return false;
        }
        const bool changed = apply();
        changed_ = changed_ || changed;
        return changed;
    }

    bool VisitFloat(const AuthoringFieldDescriptor& field, float& value, std::optional<FloatMetadata> range) {
        nlohmann::json metadata = nlohmann::json::object();
        if (range.has_value()) {
            metadata = {{"minimum", range->minimum}, {"maximum", range->maximum}, {"step", range->speed}};
        }
        const auto apply = [this, &value, range]() {
            if (!requestedValue_.is_number()) {
                SetTypeError("number");
                return false;
            }
            const float next = requestedValue_.get<float>();
            if (!std::isfinite(next)) {
                SetError("VALUE_OUT_OF_RANGE", "Number must be finite and representable as float.");
                return false;
            }
            if (range.has_value() && (next < range->minimum || next > range->maximum)) {
                SetError("VALUE_OUT_OF_RANGE", "Number must be between " + std::to_string(range->minimum) + " and " +
                                                   std::to_string(range->maximum) + ".");
                return false;
            }
            const bool changed = next != value;
            value = next;
            return changed;
        };
        return Visit(field, AuthoringFieldValueType::Float, value, std::move(metadata), apply);
    }

    template <class Validator>
    bool VisitString(const AuthoringFieldDescriptor& field, AuthoringFieldValueType type, std::string& value,
                     nlohmann::json metadata, Validator validator) {
        const auto apply = [this, &value, &validator]() {
            if (!requestedValue_.is_string()) {
                SetTypeError("string");
                return false;
            }
            const std::string next = requestedValue_.get<std::string>();
            if constexpr (!std::is_same_v<Validator, std::nullptr_t>) {
                if (!validator(next)) {
                    return false;
                }
            }
            const bool changed = next != value;
            value = next;
            return changed;
        };
        return Visit(field, type, value, std::move(metadata), apply);
    }

    bool RegisterField(const AuthoringFieldDescriptor& field, AuthoringFieldValueType type,
                       const nlohmann::json& currentValue, const nlohmann::json& metadata) {
        if (field.id.empty()) {
            SetError("EMPTY_PROPERTY_ID", "Authoring fields must have a stable property id.");
            return false;
        }
        if (!fieldIds_.insert(std::string(field.id)).second) {
            SetError("DUPLICATE_PROPERTY_ID", "Duplicate component property id: " + std::string(field.id));
            return false;
        }
        if (mode_ == Mode::Apply)
            return true;
        nlohmann::json entry{{"id", std::string(field.id)},
                             {"label", std::string(field.label)},
                             {"description", std::string(field.description)},
                             {"type", FieldTypeName(type)},
                             {"writable", field.writable},
                             {"value", currentValue}};
        for (const auto& [name, value] : metadata.items()) {
            entry[name] = value;
        }
        fields_.push_back(std::move(entry));
        return true;
    }

    static bool IsNumberArray(const nlohmann::json& value, size_t expectedSize) {
        if (!value.is_array() || value.size() != expectedSize) {
            return false;
        }
        return std::all_of(value.begin(), value.end(), [](const nlohmann::json& element) {
            return element.is_number() && std::isfinite(element.get<float>());
        });
    }

    static bool IsUnitValue(float value) noexcept { return value >= 0.0f && value <= 1.0f; }

    void SetTypeError(std::string_view expected) {
        SetError("PROPERTY_TYPE_MISMATCH",
                 "Property '" + requestedPropertyId_ + "' expects " + std::string(expected) + ".");
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

ComponentAuthoringServiceResult BuildProperties(IComponent& component, const InspectorContext& context,
                                                HeadlessComponentAuthoringBuilder& builder) {
    builder.SetContext(context);
    try {
        component.BuildInspector(builder);
    } catch (const std::exception& error) {
        return MakeFailure("COMPONENT_AUTHORING_EXCEPTION", error.what());
    } catch (...) {
        return MakeFailure("COMPONENT_AUTHORING_EXCEPTION", "Component authoring builder threw an unknown exception.");
    }
    if (builder.HasError())
        return MakeFailure(builder.Code(), builder.Message());
    ComponentAuthoringServiceResult result{};
    result.ok = true;
    result.documentChanged = builder.Changed();
    return result;
}
} // namespace

ComponentAuthoringServiceResult DescribeComponentProperties(IComponent& component, const InspectorContext& context) {
    HeadlessComponentAuthoringBuilder builder(HeadlessComponentAuthoringBuilder::Mode::Describe);
    auto result = BuildProperties(component, context, builder);
    if (result.ok) {
        result.data = {{"fields", builder.Fields()}};
    }
    return result;
}

ComponentAuthoringServiceResult ApplyComponentProperty(IComponent& component, const InspectorContext& context,
                                                       const ComponentPropertyMutation& mutation) {
    HeadlessComponentAuthoringBuilder builder(mutation.propertyId, mutation.value);
    auto result = BuildProperties(component, context, builder);
    if (result.ok && !builder.FoundRequestedProperty()) {
        return MakeFailure("PROPERTY_NOT_FOUND",
                           "Component property is unavailable in the current component state: " + mutation.propertyId);
    }
    return result;
}

} // namespace HIKARI::EDITOR
