#pragma once

#include <string>
#include <type_traits>
#include <utility>

#include <json.hpp>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFieldInfo.h"
#include "Core/Text/HIKARI_HumanizeKey.h"

// JSONFIELDS の項目表に付けるオプションと、その読み出し。保存・読み取り・正規化の本体は HIKARI_JsonFields.h。
// 編集 UI と Describe 用のオプションは保存と正規化に影響しない。文字は英語の原文で、検査器が翻訳する:
//   Label / Help / Unit      表示名・説明・単位
//   Group                    同じ Group の項目を 1 つの節にまとめる（続けて宣言する）
//   Common / Advanced        先頭に出す / 閉じた "Advanced" 節に入れる
//   ReadOnly / Color         読み取り専用 / Vec3 を色として編集する
//   AssetRef{AssetType::X}   資産参照（GUID の文字列か AssetGuid）
//   ObjectRef                物体参照（SceneObjectId。HIKARI_SceneObjectJson.h の to_json / from_json で保存する）
//   EventRef / VariableRef   実行時イベント / 実行時変数の ID（std::string）。検査器は選択欄を出す
//   Show{[](const S& s) {}}  項目を出す条件
//   Elements{表}             std::vector<構造体> の要素の項目表
//   Nested{表}               入れ子の構造体の項目表
//   Draw{fn}                 項目 1 つの編集欄を自前で描く（唯一の逃げ道。下を参照）
//
// .value に実体を持つ ID 型（AssetGuid など）はその実体の項目として扱う。独自の保存形を持つ型は nlohmann の
// to_json / from_json を定義する。
namespace HIKARI::JSONFIELDS {

struct Range {
    double min;
    double max;
    bool slider = false;
};

constexpr Range Slider(double min, double max) { return {min, max, true}; }

struct Label {
    const char* text;
};
struct Help {
    const char* text;
};
struct Unit {
    const char* text;
};
struct Group {
    const char* text;
};
struct Common {};
struct Advanced {};
struct ReadOnly {};
struct Color {};
struct ObjectRef {};
// 検査器は RuntimeEventIdPicker（ペイロードは問わない）と RuntimeVariableIdPicker（全ての値型を許す）で選ばせる。
struct EventRef {};
struct VariableRef {};

// Asset は AssetType（Core は上位層の型を知らないので型引数）。
template <class Asset>
struct AssetRef {
    Asset type;
};

// Visible はその項目を持つ構造体を受けて、出すかどうかを返す。
template <class Visible>
struct Show {
    Visible visible;
};

template <class Fields>
struct Elements {
    Fields fields;
};

template <class Fields>
struct Nested {
    Fields fields;
};

// 検査器の通常の編集欄の代わりに fn が描く。fn(builder, field, 項目の参照, その項目を持つ構造体の const 参照) が
// 変更の有無を返す。表での位置・Group・Show・ReadOnly（field.writable）は通常どおり効く。
template <class Fn>
struct Draw {
    Fn fn;
};

namespace DETAIL {

template <class T>
inline constexpr bool kIsAssetRef = false;
template <class Asset>
inline constexpr bool kIsAssetRef<AssetRef<Asset>> = true;
template <class T>
inline constexpr bool kIsElements = false;
template <class Fields>
inline constexpr bool kIsElements<Elements<Fields>> = true;
template <class T>
inline constexpr bool kIsNested = false;
template <class Fields>
inline constexpr bool kIsNested<Nested<Fields>> = true;
template <class T>
inline constexpr bool kIsDraw = false;
template <class Fn>
inline constexpr bool kIsDraw<Draw<Fn>> = true;

template <class T>
inline constexpr bool kIsVector =
    std::is_same_v<T, MATH::Vec2> || std::is_same_v<T, MATH::Vec3> || std::is_same_v<T, MATH::Vec4>;

template <class T>
inline constexpr bool kAlwaysFalse = false;

// ObjectRef は nlohmann の to_json / from_json を持つ型（SceneObjectId）にだけ付ける。持たない型は .value の数値として
// 保存されてしまい、物体参照の付け替えが効かなくなる。
template <class M, class... Options>
constexpr void CheckObjectRef() noexcept {
    if constexpr ((std::is_same_v<Options, ObjectRef> || ...)) {
        static_assert(
            std::is_constructible_v<nlohmann::json, const M&>,
            "ObjectRef needs the to_json / from_json of Scene/Serialization/Objects/HIKARI_SceneObjectJson.h");
    }
}

} // namespace DETAIL

// ID 型（AssetGuid・VfxSlotId など）は .value を実体とする。nlohmann が直接変換できる型（SceneObjectId）は除く。
template <class T>
concept IdWrapper =
    std::is_class_v<T> && !std::is_constructible_v<nlohmann::json, const T&> && requires(T& wrapper) { wrapper.value; };

template <class T>
constexpr auto& Unwrap(T& value) noexcept {
    if constexpr (IdWrapper<std::remove_const_t<T>>) {
        return value.value;
    } else {
        return value;
    }
}

template <class M>
using ValueOf = std::remove_cvref_t<decltype(Unwrap(std::declval<M&>()))>;

// 項目に付けたオプションの集計。検査器と Describe が同じ読み方をする。
struct Meta {
    const char* label = "";
    const char* help = "";
    const char* unit = "";
    const char* group = "";
    bool common = false;
    bool advanced = false;
    bool readOnly = false;
    bool color = false;
    bool hasRange = false;
    Range range{};
    bool hasShow = false;
    // AssetRef を付けた項目の AssetType の値。
    int assetType = -1;
};

namespace DETAIL {

// 列挙名の表のように Meta に載らないオプションは無視する。
template <class Option>
void Collect(Meta&, const Option&) {}
inline void Collect(Meta& meta, const Range& option) {
    meta.hasRange = true;
    meta.range = option;
}
inline void Collect(Meta& meta, const Label& option) { meta.label = option.text; }
inline void Collect(Meta& meta, const Help& option) { meta.help = option.text; }
inline void Collect(Meta& meta, const Unit& option) { meta.unit = option.text; }
inline void Collect(Meta& meta, const Group& option) { meta.group = option.text; }
inline void Collect(Meta& meta, const Common&) { meta.common = true; }
inline void Collect(Meta& meta, const Advanced&) { meta.advanced = true; }
inline void Collect(Meta& meta, const ReadOnly&) { meta.readOnly = true; }
inline void Collect(Meta& meta, const Color&) { meta.color = true; }
template <class Asset>
void Collect(Meta& meta, const AssetRef<Asset>& option) {
    meta.assetType = static_cast<int>(option.type);
}
template <class Visible>
void Collect(Meta& meta, const Show<Visible>&) {
    meta.hasShow = true;
}

} // namespace DETAIL

template <class... Options>
Meta MetaOf(const Options&... options) {
    Meta meta;
    (DETAIL::Collect(meta, options), ...);
    return meta;
}

// Label が無ければキーから作った表示名。
inline std::string LabelOf(const Meta& meta, const char* key) {
    return *meta.label != '\0' ? std::string(meta.label) : TEXT::HumanizeKey(key);
}

// Elements / Nested が持つ副表（その構造体の項目表）。
template <class Option, class... Rest>
constexpr const auto& SubFields(const Option& option, [[maybe_unused]] const Rest&... rest) {
    if constexpr (DETAIL::kIsElements<Option> || DETAIL::kIsNested<Option>) {
        return option.fields;
    } else {
        return SubFields(rest...);
    }
}

// Draw に書いた関数。
template <class Option, class... Rest>
constexpr const auto& DrawOf(const Option& option, [[maybe_unused]] const Rest&... rest) {
    if constexpr (DETAIL::kIsDraw<Option>) {
        return option.fn;
    } else {
        return DrawOf(rest...);
    }
}

// AssetRef に書いた AssetType の値。
template <class Option, class... Rest>
constexpr auto AssetTypeOf(const Option& option, [[maybe_unused]] const Rest&... rest) {
    if constexpr (DETAIL::kIsAssetRef<Option>) {
        return option.type;
    } else {
        return AssetTypeOf(rest...);
    }
}

// 列挙（名前の表を持つ項目）以外の項目の種類。M はメンバの型、Options は項目に付けたオプションの型。
template <class M, class... Options>
constexpr FieldKind KindOf() noexcept {
    if constexpr ((DETAIL::kIsElements<Options> || ...)) {
        return FieldKind::Elements;
    } else if constexpr ((DETAIL::kIsNested<Options> || ...)) {
        return FieldKind::Nested;
    } else if constexpr ((DETAIL::kIsAssetRef<Options> || ...)) {
        return FieldKind::AssetRef;
    } else if constexpr ((std::is_same_v<Options, ObjectRef> || ...)) {
        DETAIL::CheckObjectRef<M, Options...>();
        return FieldKind::ObjectRef;
    } else if constexpr ((std::is_same_v<Options, EventRef> || ...)) {
        static_assert(std::is_same_v<ValueOf<M>, std::string>, "EventRef is for std::string members");
        return FieldKind::EventRef;
    } else if constexpr ((std::is_same_v<Options, VariableRef> || ...)) {
        static_assert(std::is_same_v<ValueOf<M>, std::string>, "VariableRef is for std::string members");
        return FieldKind::VariableRef;
    } else if constexpr (std::is_same_v<ValueOf<M>, bool>) {
        return FieldKind::Bool;
    } else if constexpr (std::is_integral_v<ValueOf<M>>) {
        return FieldKind::Int;
    } else if constexpr (std::is_floating_point_v<ValueOf<M>>) {
        return FieldKind::Float;
    } else if constexpr (std::is_same_v<ValueOf<M>, MATH::Vec2>) {
        return FieldKind::Vec2;
    } else if constexpr (std::is_same_v<ValueOf<M>, MATH::Vec3>) {
        return FieldKind::Vec3;
    } else if constexpr (std::is_same_v<ValueOf<M>, std::string>) {
        return FieldKind::String;
    } else {
        static_assert(DETAIL::kAlwaysFalse<M>, "This member type is not supported by JSONFIELDS inspection");
    }
}

} // namespace HIKARI::JSONFIELDS
