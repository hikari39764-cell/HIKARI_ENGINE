#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "HIKARI/Project/ActionTypes.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/UiTypes.h"
#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Project/VfxTypes.h"

namespace HIKARI {

struct GameProjectSdkAccess;

// 資産 GUID 項目の保存形式（Scene 上の {"$hikari.assetGuid": "<guid>"}）。Cook の依存収集もこの印を読む。
inline constexpr std::string_view kGameProjectAssetGuidArchiveMarker = "$hikari.assetGuid";

// 同じ宣言を読込・保存・Inspector の三つの用途で順に評価する。
enum class PropertyFieldsMode : uint8_t {
    Load,
    Save,
    Inspect,
};

// GUID で参照する資産の種類。保存形式は共通で、Inspector の選択候補だけが変わる。
enum class GameProjectAssetKind : uint8_t {
    Scene,
    Prefab,
    Model,
    ModelMaterialSet,
    Material,
    Texture,
    Sky,
    Sprite,
    SpriteMaterial,
    Animation,
    AnimationStateMachine,
    Sequence,
    Dialogue,
    GameplayCamera,
    VfxEffect,
    Shader,
    PostEffect,
    PostProfile,
    Font,
    TextStyle,
    UiAnimation,
};

// 呼び出し側が所有する固定長文字列。DLL 境界を越えて STL の所有権を渡さないための薄い参照。
class GameProjectTextBuffer {
public:
    template <size_t N>
    GameProjectTextBuffer(std::array<char, N>& value) noexcept : data_(value.data()), capacity_(N) {}
    template <size_t N>
    GameProjectTextBuffer(char (&value)[N]) noexcept : data_(value), capacity_(N) {}
    GameProjectTextBuffer(char* data, size_t capacity) noexcept : data_(data), capacity_(capacity) {}

    char* Data() const noexcept { return data_; }
    size_t Capacity() const noexcept { return capacity_; }

private:
    char* data_ = nullptr;
    size_t capacity_ = 0u;
};

enum class GameProjectTextFieldKind : uint8_t {
    Plain,
    Asset,
    AssetId,
    InputAction,
    Variable,
    Event,
};

struct GameProjectTextFieldDesc {
    std::string_view id{};
    std::string_view label{};
    GameProjectTextFieldKind kind = GameProjectTextFieldKind::Plain;
    GameProjectAssetKind assetKind = GameProjectAssetKind::Scene;
    GameProjectInputValueType inputType = GameProjectInputValueType::Button;
    GameProjectBindingValueType valueType = GameProjectBindingValueType::Float;
    bool eventHasPayload = false;
};

// 一つの宣言で、シーンへの保存形式・読込時の検証・Inspector 表示・AI/オーサリング操作の ID を揃える。
// 各関数は値が変わったとき true。Load では保存値が無い項目は現在値（初期値）を保つ。
// 範囲付きの数値は読込時にも範囲へ収め、非有限値は現在値を保つ。
// Section は Load/Save では常に true を返し、内側の項目を必ず処理する。
class PropertyFields {
public:
    PropertyFieldsMode GetMode() const noexcept { return mode_; }
    bool IsLoading() const noexcept { return mode_ == PropertyFieldsMode::Load; }
    bool IsInspecting() const noexcept { return mode_ == PropertyFieldsMode::Inspect; }

    HIKARI_GAME_PROJECT_SDK_API bool Bool(std::string_view id, std::string_view label, bool& value);
    HIKARI_GAME_PROJECT_SDK_API bool Int(std::string_view id, std::string_view label, int& value,
                                         int minimum = (std::numeric_limits<int>::min)(),
                                         int maximum = (std::numeric_limits<int>::max)());
    HIKARI_GAME_PROJECT_SDK_API bool Float(std::string_view id, std::string_view label, float& value);
    HIKARI_GAME_PROJECT_SDK_API bool Float(std::string_view id, std::string_view label, float& value, float minimum,
                                           float maximum, float speed = 0.1f);
    HIKARI_GAME_PROJECT_SDK_API bool Float3(std::string_view id, std::string_view label, GameProjectFloat3& value);
    HIKARI_GAME_PROJECT_SDK_API bool Color3(std::string_view id, std::string_view label, GameProjectFloat3& value);
    // シーン内オブジェクトの参照。複製・Prefab 展開時に ID が付け替えられる。
    HIKARI_GAME_PROJECT_SDK_API bool SceneObject(std::string_view id, std::string_view label, uint64_t& sceneObjectId);
    HIKARI_GAME_PROJECT_SDK_API bool Vfx(std::string_view id, std::string_view label,
                                         GameProjectVfxAssetReference& reference);
    HIKARI_GAME_PROJECT_SDK_API bool Texture(std::string_view id, std::string_view label,
                                             GameProjectUiTextureReference& reference);

    bool Text(std::string_view id, std::string_view label, GameProjectTextBuffer value) {
        return TextField({id, label}, value.Data(), value.Capacity());
    }
    bool Text(std::string_view id, std::string_view label, std::string& value) {
        return TextString({id, label}, value);
    }
    bool Asset(std::string_view id, std::string_view label, GameProjectAssetKind kind, GameProjectTextBuffer guid) {
        return TextField({id, label, GameProjectTextFieldKind::Asset, kind}, guid.Data(), guid.Capacity());
    }
    bool Asset(std::string_view id, std::string_view label, GameProjectAssetKind kind, std::string& guid) {
        return TextString({id, label, GameProjectTextFieldKind::Asset, kind}, guid);
    }
    // Engine が既定のキー名で読む資産 GUID。素の文字列で保存し、ID が AssetGuid で終われば Cook が依存に数える。
    bool AssetId(std::string_view id, std::string_view label, GameProjectAssetKind kind, GameProjectTextBuffer guid) {
        return TextField({id, label, GameProjectTextFieldKind::AssetId, kind}, guid.Data(), guid.Capacity());
    }
    bool AssetId(std::string_view id, std::string_view label, GameProjectAssetKind kind, std::string& guid) {
        return TextString({id, label, GameProjectTextFieldKind::AssetId, kind}, guid);
    }
    bool InputAction(std::string_view id, std::string_view label, GameProjectInputValueType type,
                     GameProjectTextBuffer actionId) {
        GameProjectTextFieldDesc field{id, label, GameProjectTextFieldKind::InputAction};
        field.inputType = type;
        return TextField(field, actionId.Data(), actionId.Capacity());
    }
    bool InputAction(std::string_view id, std::string_view label, GameProjectInputValueType type,
                     std::string& actionId) {
        GameProjectTextFieldDesc field{id, label, GameProjectTextFieldKind::InputAction};
        field.inputType = type;
        return TextString(field, actionId);
    }
    bool Variable(std::string_view id, std::string_view label, GameProjectBindingValueType type,
                  GameProjectTextBuffer variableId) {
        GameProjectTextFieldDesc field{id, label, GameProjectTextFieldKind::Variable};
        field.valueType = type;
        return TextField(field, variableId.Data(), variableId.Capacity());
    }
    bool Variable(std::string_view id, std::string_view label, GameProjectBindingValueType type,
                  std::string& variableId) {
        GameProjectTextFieldDesc field{id, label, GameProjectTextFieldKind::Variable};
        field.valueType = type;
        return TextString(field, variableId);
    }
    bool Event(std::string_view id, std::string_view label, bool hasPayload, GameProjectBindingValueType payloadType,
               GameProjectTextBuffer eventId) {
        GameProjectTextFieldDesc field{id, label, GameProjectTextFieldKind::Event};
        field.valueType = payloadType;
        field.eventHasPayload = hasPayload;
        return TextField(field, eventId.Data(), eventId.Capacity());
    }
    bool Event(std::string_view id, std::string_view label, bool hasPayload, GameProjectBindingValueType payloadType,
               std::string& eventId) {
        GameProjectTextFieldDesc field{id, label, GameProjectTextFieldKind::Event};
        field.valueType = payloadType;
        field.eventHasPayload = hasPayload;
        return TextString(field, eventId);
    }

    // Inspector 専用の表示。Load/Save では何もしない（Section は true、Button は false）。
    HIKARI_GAME_PROJECT_SDK_API bool Section(std::string_view label, bool defaultOpen = false);
    HIKARI_GAME_PROJECT_SDK_API void Note(std::string_view text);
    HIKARI_GAME_PROJECT_SDK_API bool Button(std::string_view label);

    // 固定長バッファ版の本体。収まらない保存値は切り詰めず空にする。
    HIKARI_GAME_PROJECT_SDK_API bool TextField(const GameProjectTextFieldDesc& field, char* value, size_t capacity);

private:
    friend struct GameProjectSdkAccess;

    static constexpr size_t kInspectorTextHeadroom = 256u;

    PropertyFields(PropertyFieldsMode mode, void* archive, void* inspector) noexcept
        : mode_(mode), archive_(archive), inspector_(inspector) {}

    HIKARI_GAME_PROJECT_SDK_API size_t StoredTextSize(std::string_view id) const noexcept;

    bool TextString(const GameProjectTextFieldDesc& field, std::string& value) {
        size_t capacity = value.size() + 1u;
        if (mode_ == PropertyFieldsMode::Load) {
            const size_t stored = StoredTextSize(field.id) + 1u;
            capacity = stored > capacity ? stored : capacity;
        } else if (mode_ == PropertyFieldsMode::Inspect) {
            capacity += kInspectorTextHeadroom;
        }
        std::string buffer(capacity, '\0');
        std::memcpy(buffer.data(), value.data(), value.size());
        if (!TextField(field, buffer.data(), buffer.size())) {
            return false;
        }
        buffer.resize(std::strlen(buffer.c_str()));
        value = std::move(buffer);
        return true;
    }

    PropertyFieldsMode mode_ = PropertyFieldsMode::Load;
    void* archive_ = nullptr;
    void* inspector_ = nullptr;
};

} // namespace HIKARI
