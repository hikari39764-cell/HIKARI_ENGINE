#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/ActionTypes.h"
#include "HIKARI/Project/AssetKind.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/UiTypes.h"
#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Project/VfxTypes.h"

namespace HIKARI {

struct GameProjectSdkAccess;

// 資産 GUID 項目の保存形式（Scene 上の {"$hikari.assetGuid": "<guid>"}）。Cook の依存収集は印ではなく、GUID 形式の値で拾う。
inline constexpr std::string_view kGameProjectAssetGuidArchiveMarker = "$hikari.assetGuid";

// 同じ宣言を読込・保存・Inspector の三つの用途で順に評価する。
enum class PropertyFieldsMode : uint8_t {
    Load,
    Save,
    Inspect,
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

// 列挙値と保存名の組（PropertyFields::Choice 用）。保存名は変えない識別子で、Inspector にもそのまま表示する。
template <class Enum>
struct GameProjectEnumName {
    Enum value{};
    std::string_view name{};
};

struct GameProjectTextFieldDesc {
    std::string_view id{};
    std::string_view label{};
    GameProjectTextFieldKind kind = GameProjectTextFieldKind::Plain;
    GameProjectAssetKind assetKind = GameProjectAssetKind::Scene;
    GameProjectInputValueType inputType = GameProjectInputValueType::Button;
    GameProjectBindingValueType valueType = GameProjectBindingValueType::Float;
    bool eventHasPayload = false;
    // kind が Asset で空でない時、assetKind の代わりに、モジュールが清単に登記した資産の種類（typeId）で候補を絞る。
    std::string_view moduleAssetTypeId{};
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
    // 名前付きの選択肢。保存値は選んだ名前なので、並び順を変えても保存済みの Scene は変わらない。
    // 知らない保存名は現在値を保ち、index が範囲外なら保存しない。空・重複した名前を含む表は受け付けない。
    HIKARI_GAME_PROJECT_SDK_API bool Choice(std::string_view id, std::string_view label, int& index,
                                            std::span<const std::string_view> names);
    // 列挙値版。表に無い現在値は保存せず、Inspector で選び直すまで保つ。
    template <class Enum, size_t N>
    bool Choice(std::string_view id, std::string_view label, Enum& value, const GameProjectEnumName<Enum> (&names)[N]) {
        static_assert(std::is_enum_v<Enum>);
        std::array<std::string_view, N> choiceNames{};
        int index = -1;
        for (size_t entry = 0u; entry < N; ++entry) {
            choiceNames[entry] = names[entry].name;
            if (index < 0 && names[entry].value == value) {
                index = static_cast<int>(entry);
            }
        }
        if (!Choice(id, label, index, choiceNames) || index < 0 || static_cast<size_t>(index) >= N) {
            return false;
        }
        value = names[static_cast<size_t>(index)].value;
        return true;
    }

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
    // モジュールが清単に登記した種類（assetTypes の typeId）の資産。保存形式は Asset と同じ。
    bool ModuleAsset(std::string_view id, std::string_view label, std::string_view moduleAssetTypeId,
                     GameProjectTextBuffer guid) {
        GameProjectTextFieldDesc field{id, label, GameProjectTextFieldKind::Asset};
        field.moduleAssetTypeId = moduleAssetTypeId;
        return TextField(field, guid.Data(), guid.Capacity());
    }
    bool ModuleAsset(std::string_view id, std::string_view label, std::string_view moduleAssetTypeId,
                     std::string& guid) {
        GameProjectTextFieldDesc field{id, label, GameProjectTextFieldKind::Asset};
        field.moduleAssetTypeId = moduleAssetTypeId;
        return TextString(field, guid);
    }
    // Engine が既定のキー名で読む資産 GUID。素の文字列で保存する（GUID 形式の値は Cook が依存に数える）。
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

// 項目の宣言。部品（IGameComponent::DescribeProperties）以外のデータ、たとえばモジュールが自分のファイルに書く
// 設定も、同じ Describe 一つで読込・保存・画面（IEditorWorkspaceUi::Fields）を済ませられる。
class IPropertyFieldDeclaration {
public:
    virtual void Describe(PropertyFields& fields) = 0;

protected:
    ~IPropertyFieldDeclaration() = default;
};

// SavePropertyFields の結果を受け取る。文字はこの呼び出しの間だけ有効。
class IPropertyFieldsTextSink {
public:
    virtual void Receive(std::string_view utf8Json) = 0;

protected:
    ~IPropertyFieldsTextSink() = default;
};

// JSON のオブジェクト（UTF-8 の文字）へ宣言を当てて読む。オブジェクトとして読めない文字なら false で、宣言は呼ばない。
// 無い項目は現在値のまま残る（PropertyFields の Load と同じ）。
HIKARI_GAME_PROJECT_SDK_API bool LoadPropertyFields(std::string_view utf8Json,
                                                    IPropertyFieldDeclaration& declaration) noexcept;
// 宣言を保存用に評価し、JSON のオブジェクト（UTF-8 の文字）を sink へ渡す。
HIKARI_GAME_PROJECT_SDK_API bool SavePropertyFields(IPropertyFieldDeclaration& declaration,
                                                    IPropertyFieldsTextSink& sink) noexcept;

} // namespace HIKARI
