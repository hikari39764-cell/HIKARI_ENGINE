#pragma once

#include <cstddef>
#include <string_view>

#include "HIKARI/Project/ActionTypes.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/UiTypes.h"
#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Project/VfxTypes.h"

namespace HIKARI {

struct GameProjectSdkAccess;

class GameProjectInspector {
public:
    HIKARI_GAME_PROJECT_SDK_API void Text(std::string_view text);
    HIKARI_GAME_PROJECT_SDK_API bool Section(std::string_view label, bool defaultOpen = false);
    // Mutate the component's authored fields when true; the normal Inspector
    // transaction captures all resulting property changes as one Undo step.
    HIKARI_GAME_PROJECT_SDK_API bool Button(std::string_view label);
    HIKARI_GAME_PROJECT_SDK_API bool Bool(std::string_view label, bool& value);
    HIKARI_GAME_PROJECT_SDK_API bool Int(std::string_view label, int& value);
    HIKARI_GAME_PROJECT_SDK_API bool Float(std::string_view label, float& value);
    HIKARI_GAME_PROJECT_SDK_API bool FloatRange(std::string_view label, float& value, float minimum, float maximum,
                                                float speed = 0.1f);
    HIKARI_GAME_PROJECT_SDK_API bool Color3(std::string_view label, float& red, float& green, float& blue);
    // Stable property IDs connect the same field to Inspector and the
    // engine's authoring/AI operations. IDs belong to the component schema,
    // not to translated display labels. Existing label-only overloads remain.
    HIKARI_GAME_PROJECT_SDK_API bool Bool(std::string_view propertyId, std::string_view label, bool& value);
    HIKARI_GAME_PROJECT_SDK_API bool Int(std::string_view propertyId, std::string_view label, int& value);
    HIKARI_GAME_PROJECT_SDK_API bool Float(std::string_view propertyId, std::string_view label, float& value);
    HIKARI_GAME_PROJECT_SDK_API bool FloatRange(std::string_view propertyId, std::string_view label, float& value,
                                                float minimum, float maximum, float speed = 0.1f);
    HIKARI_GAME_PROJECT_SDK_API bool Color3(std::string_view propertyId, std::string_view label, float& red,
                                            float& green, float& blue);
    HIKARI_GAME_PROJECT_SDK_API bool SceneObjectId(std::string_view propertyId, std::string_view label,
                                                   uint64_t& value);
    HIKARI_GAME_PROJECT_SDK_API bool StringBuffer(std::string_view label, char* value, size_t capacity);
    HIKARI_GAME_PROJECT_SDK_API bool StringBuffer(std::string_view propertyId, std::string_view label, char* value,
                                                  size_t capacity);
    HIKARI_GAME_PROJECT_SDK_API bool SceneAssetGuid(std::string_view propertyId, std::string_view label, char* value,
                                                    size_t capacity);
    HIKARI_GAME_PROJECT_SDK_API bool SequenceAssetGuid(std::string_view propertyId, std::string_view label, char* value,
                                                       size_t capacity);
    HIKARI_GAME_PROJECT_SDK_API bool PrefabAssetGuid(std::string_view propertyId, std::string_view label, char* value,
                                                     size_t capacity);
    HIKARI_GAME_PROJECT_SDK_API bool InputActionId(std::string_view propertyId, std::string_view label,
                                                   GameProjectInputValueType expectedType, char* value,
                                                   size_t capacity);
    // Typed picker for the formal model/material binding asset. The value
    // remains a caller-owned fixed buffer so no STL state crosses the SDK
    // boundary.
    HIKARI_GAME_PROJECT_SDK_API bool ModelMaterialSetAssetGuid(std::string_view propertyId, std::string_view label,
                                                               char* value, size_t capacity);
    HIKARI_GAME_PROJECT_SDK_API bool Vec2(std::string_view label, float& x, float& y);
    HIKARI_GAME_PROJECT_SDK_API bool VariableBinding(std::string_view label, GameProjectBindingValueType expectedType,
                                                     char* value, size_t capacity);
    HIKARI_GAME_PROJECT_SDK_API bool EventBinding(std::string_view label, bool hasPayload,
                                                  GameProjectBindingValueType payloadType, char* value,
                                                  size_t capacity);
    HIKARI_GAME_PROJECT_SDK_API bool VfxAssetReference(std::string_view label, GameProjectVfxAssetReference& reference);
    HIKARI_GAME_PROJECT_SDK_API bool VfxAssetReference(std::string_view propertyId, std::string_view label,
                                                       GameProjectVfxAssetReference& reference);
    HIKARI_GAME_PROJECT_SDK_API bool TextureAssetReference(std::string_view label,
                                                           GameProjectUiTextureReference& reference);
    HIKARI_GAME_PROJECT_SDK_API bool TextureAssetReference(std::string_view propertyId, std::string_view label,
                                                           GameProjectUiTextureReference& reference);

    bool AssetReference(std::string_view label, GameProjectVfxAssetReference& reference) {
        return VfxAssetReference(label, reference);
    }

private:
    friend struct GameProjectSdkAccess;

    explicit GameProjectInspector(void* builder) noexcept : builder_(builder) {}

    void* builder_ = nullptr;
};

} // namespace HIKARI
