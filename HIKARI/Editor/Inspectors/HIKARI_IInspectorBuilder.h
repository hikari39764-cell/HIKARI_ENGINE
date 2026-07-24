#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace HIKARI {

    namespace INPUT {
        enum class InputActionValueType;
    }

    enum class AssetType;
    class AssetDatabase;
    class AssetRegistry;
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
    };

    class IInspectorBuilder {
    public:
        virtual ~IInspectorBuilder() = default;

        virtual void SetContext(const InspectorContext& context) = 0;
        virtual const InspectorContext& GetContext() const noexcept = 0;

        virtual void Text(std::string_view text) = 0;
        virtual bool Button(std::string_view label) = 0;
        virtual bool Bool(std::string_view label, bool& value) = 0;
        virtual bool Int(std::string_view label, int& value) = 0;
        virtual bool Float(std::string_view label, float& value) = 0;
        virtual bool FloatRange(
            std::string_view label,
            float& value,
            float minimum,
            float maximum,
            float speed = 0.1f) = 0;
        virtual bool String(std::string_view label, std::string& value) = 0;
        virtual bool InputActionIdPicker(
            std::string_view label,
            INPUT::InputActionValueType expectedType,
            std::string& value) = 0;
        virtual bool Choice(
            std::string_view label,
            int& selectedIndex,
            std::span<const char* const> choices) = 0;
        virtual bool Vec2(std::string_view label, float& x, float& y) = 0;
        virtual bool AssetIdPicker(std::string_view label, AssetType assetType, std::string& value) = 0;
        virtual bool SceneIdPicker(std::string_view label, std::string& value) = 0;
        virtual bool SceneObjectIdPicker(
            std::string_view label,
            SceneObjectId& value) = 0;
    };

} // namespace HIKARI
