#pragma once

#include "Authoring/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

class ImGuiInspectorBuilder final : public IInspectorBuilder {
public:
    void SetContext(const InspectorContext& context) override;
    const InspectorContext& GetContext() const noexcept override;

    void Text(std::string_view text) override;
    bool Section(std::string_view label, bool defaultOpen = false) override;
    bool Button(std::string_view label) override;
    bool Bool(const AuthoringFieldDescriptor& field, bool& value) override;
    bool Int(const AuthoringFieldDescriptor& field, int& value) override;
    bool Float(const AuthoringFieldDescriptor& field, float& value) override;
    bool FloatRange(const AuthoringFieldDescriptor& field, float& value, float minimum, float maximum,
                    float speed = 0.1f) override;
    bool TypedValue(const AuthoringFieldDescriptor& field, BINDING::Value& value, bool hasRange, float minimum,
                    float maximum, float step) override;
    bool Color3(const AuthoringFieldDescriptor& field, float& red, float& green, float& blue) override;
    bool Vec2(const AuthoringFieldDescriptor& field, float& x, float& y) override;
    bool Vec3(const AuthoringFieldDescriptor& field, float& x, float& y, float& z) override;
    bool String(const AuthoringFieldDescriptor& field, std::string& value) override;
    bool MultilineString(const AuthoringFieldDescriptor& field, std::string& value) override;
    bool InputActionIdPicker(const AuthoringFieldDescriptor& field, INPUT::InputActionValueType expectedType,
                             std::string& value) override;
    using IInspectorBuilder::RuntimeVariableIdPicker;
    bool RuntimeVariableIdPicker(const AuthoringFieldDescriptor& field,
                                 std::span<const BINDING::ValueType> allowedTypes, std::string& value) override;
    bool RuntimeEventIdPicker(const AuthoringFieldDescriptor& field, bool hasPayload, BINDING::ValueType payloadType,
                              std::string& value) override;
    bool RuntimeEventIdPicker(const AuthoringFieldDescriptor& field, AuthoringEventPayloadPolicy policy,
                              BINDING::ValueType payloadType, std::string& value) override;
    bool Choice(const AuthoringFieldDescriptor& field, int& selectedIndex,
                std::span<const AuthoringChoice> choices) override;
    bool AssetIdPicker(const AuthoringFieldDescriptor& field, AssetType assetType, std::string& value) override;
    bool ModuleAssetIdPicker(const AuthoringFieldDescriptor& field, std::string_view moduleAssetTypeId,
                             std::string& value) override;
    bool SceneIdPicker(const AuthoringFieldDescriptor& field, std::string& value) override;
    bool SceneObjectIdPicker(const AuthoringFieldDescriptor& field, SceneObjectId& value) override;

private:
    InspectorContext context_{};
};

} // namespace HIKARI
