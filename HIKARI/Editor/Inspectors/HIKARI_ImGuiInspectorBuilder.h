#pragma once

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    class ImGuiInspectorBuilder final : public IInspectorBuilder {
    public:
        void SetContext(const InspectorContext& context) override;
        const InspectorContext& GetContext() const noexcept override;

        void Text(std::string_view text) override;
        bool Button(std::string_view label) override;
        bool Bool(std::string_view label, bool& value) override;
        bool Int(std::string_view label, int& value) override;
        bool Float(std::string_view label, float& value) override;
        bool FloatRange(
            std::string_view label,
            float& value,
            float minimum,
            float maximum,
            float speed = 0.1f) override;
        bool String(std::string_view label, std::string& value) override;
        bool InputActionIdPicker(
            std::string_view label,
            INPUT::InputActionValueType expectedType,
            std::string& value) override;
        bool Choice(
            std::string_view label,
            int& selectedIndex,
            std::span<const char* const> choices) override;
        bool Vec2(std::string_view label, float& x, float& y) override;
        bool AssetIdPicker(std::string_view label, AssetType assetType, std::string& value) override;
        bool SceneIdPicker(std::string_view label, std::string& value) override;
        bool SceneObjectIdPicker(
            std::string_view label,
            SceneObjectId& value) override;

    private:
        InspectorContext context_{};
    };

} // namespace HIKARI
