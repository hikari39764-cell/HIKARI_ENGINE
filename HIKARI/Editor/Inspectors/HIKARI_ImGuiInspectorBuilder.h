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
    bool Bool(std::string_view label, bool& value) override;
    bool Int(std::string_view label, int& value) override;
    bool Float(std::string_view label, float& value) override;
    bool FloatRange(std::string_view label, float& value, float minimum, float maximum, float speed = 0.1f) override;
    bool Color3(std::string_view label, float& red, float& green, float& blue) override;
    bool String(std::string_view label, std::string& value) override;
    bool MultilineString(const AuthoringFieldDescriptor& field, std::string& value) override;
    bool InputActionIdPicker(std::string_view label, INPUT::InputActionValueType expectedType,
                             std::string& value) override;
    bool RuntimeVariableIdPicker(std::string_view label, BINDING::ValueType expectedType, std::string& value) override;
    bool RuntimeEventIdPicker(std::string_view label, bool hasPayload, BINDING::ValueType payloadType,
                              std::string& value) override;
    bool RuntimeEventIdPicker(std::string_view label, AuthoringEventPayloadPolicy policy,
                              BINDING::ValueType payloadType, std::string& value) override;
    bool Choice(std::string_view label, int& selectedIndex, std::span<const char* const> choices) override;
    bool Vec2(std::string_view label, float& x, float& y) override;
    bool AssetIdPicker(std::string_view label, AssetType assetType, std::string& value) override;
    bool SceneIdPicker(std::string_view label, std::string& value) override;
    bool SceneObjectIdPicker(std::string_view label, SceneObjectId& value) override;

private:
    InspectorContext context_{};
};

} // namespace HIKARI
