#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonFieldInfo.h"
#include "Core/Serialization/Json/HIKARI_JsonFieldOptions.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Math/HIKARI_Math3D.h"

// 設定構造体のフィールドを 1 か所で宣言し、JSON 読み書き・値の正規化・説明（Describe）・検査器・パス指定を共有する。
//
//   constexpr auto kFogFields = [](auto&& field) {
//       field("enabled", &FogSettings::enabled);
//       field("density", &FogSettings::density, JSONFIELDS::Range{0.0, 1.0});
//       field("mode", &FogSettings::mode, kFogModeNames);
//   };
//   JSONFIELDS::Write(out, fog, kFogFields);
//   JSONFIELDS::Read(in, fog, kFogFields);            // 厳密: 型違いや未知の列挙名は項目のパス付きの std::runtime_error（null は指定なし）
//   JSONFIELDS::ReadLenient(in, fog, kFogFields);     // 型不一致は現在値を保持（例外なし）
//   fog = JSONFIELDS::Sanitize(fog, kFogFields);      // Range: 非有限値は既定値、範囲外は丸め（Vec は成分ごと）
//
// Slider(min, max) は Range と同じ範囲で、編集 UI では滑块で出す（全域に意味がある強さ・混合・角度など）。
// Vec2/3/4 は配列で保存し、読み込みは配列と {x,y,z,w} の両方を受け付ける。
//
// 項目に付けるオプション（Label・Group・Common・Show・Elements など）は HIKARI_JsonFieldOptions.h。
// Elements / Nested の入れ子も同じ関数で読み書きする。Describe は項目の静的な説明（FieldInfo）を作る。
// 項目は構造体自身か、その基底クラスのメンバ。基底の構造体の表は、派生構造体の表の中から呼んでそのまま使える。
namespace HIKARI::JSONFIELDS {

// Write / Sanitize / Describe は入れ子の構造体で自分自身を呼ぶ。
template <class T, class Fields>
void Write(nlohmann::json& out, const T& object, const Fields& fields);
template <class T, class Fields>
T Sanitize(T object, const Fields& fields, const T& defaults = T{});
template <class T, class Fields>
std::vector<FieldInfo> Describe(const Fields& fields);

namespace DETAIL {

template <class T>
nlohmann::json ToJson(const T& value) {
    if constexpr (std::is_same_v<T, MATH::Vec2>) {
        return nlohmann::json::array({value.x, value.y});
    } else if constexpr (std::is_same_v<T, MATH::Vec3>) {
        return nlohmann::json::array({value.x, value.y, value.z});
    } else if constexpr (std::is_same_v<T, MATH::Vec4>) {
        return nlohmann::json::array({value.x, value.y, value.z, value.w});
    } else {
        return value;
    }
}

template <class T>
void ReadVector(const nlohmann::json& in, const char* key, T& value) {
    if (const nlohmann::json* node = JSONREAD::FindMember(in, key)) {
        if constexpr (std::is_same_v<T, MATH::Vec2>) {
            value = JSONREAD::Vec2Or(*node, value);
        } else if constexpr (std::is_same_v<T, MATH::Vec3>) {
            value = JSONREAD::Vec3Or(*node, value);
        } else {
            value = JSONREAD::Vec4Or(*node, value);
        }
    }
}

template <class T>
void ReadLenient(const nlohmann::json& in, const char* key, T& value) {
    if constexpr (kIsVector<T>) {
        ReadVector(in, key, value);
    } else if constexpr (std::is_same_v<T, bool>) {
        value = JSONREAD::BooleanMemberOr(in, key, value);
    } else if constexpr (std::is_same_v<T, int>) {
        value = JSONREAD::IntegerMemberOr(in, key, value);
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        value = JSONREAD::UnsignedMemberOr(in, key, value);
    } else if constexpr (std::is_same_v<T, double>) {
        // double の項目（緯度・時刻など）は float を経由させない。
        const nlohmann::json* node = JSONREAD::FindMember(in, key);
        value = node != nullptr && node->is_number() ? node->get<double>() : value;
    } else if constexpr (std::is_floating_point_v<T>) {
        value = JSONREAD::NumberMemberOr(in, key, value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        value = JSONREAD::StringMemberOr(in, key, std::move(value));
    } else if (const nlohmann::json* node = JSONREAD::FindMember(in, key)) {
        // nlohmann が変換できる他の型（uint64_t・物体参照など）。読めなければ現在値を保持する。
        try {
            value = node->get<T>();
        } catch (const std::exception&) {
        }
    }
}

// 配列は成分の数が合い全て数値、{x,y,z,w} は与えた成分が全て数値（欠けた成分は現在値）。
template <class T>
void AssignVector(const nlohmann::json& node, T& value) {
    constexpr size_t kCount = std::is_same_v<T, MATH::Vec2> ? 2u : (std::is_same_v<T, MATH::Vec3> ? 3u : 4u);
    constexpr const char* kNames[] = {"x", "y", "z", "w"};
    T parsed = value;
    float* components = &parsed.x;
    const auto number = [](const nlohmann::json& element) {
        const float parsedNumber = element.is_number() ? element.get<float>() : 0.0f;
        if (!element.is_number() || !std::isfinite(parsedNumber)) {
            throw std::runtime_error("expects finite numbers");
        }
        return parsedNumber;
    };
    if (node.is_array() && node.size() == kCount) {
        for (size_t index = 0; index < kCount; ++index) {
            components[index] = number(node[index]);
        }
    } else if (node.is_object()) {
        for (size_t index = 0; index < kCount; ++index) {
            if (const nlohmann::json* member = JSONREAD::FindMember(node, kNames[index])) {
                components[index] = number(*member);
            }
        }
    } else {
        throw std::runtime_error("expects an array of " + std::to_string(kCount) + " numbers");
    }
    value = parsed;
}

// 厳密な読み取りと SetField が共有する 1 項目の変換。型違い・範囲外は std::runtime_error（value は変えない）。
template <class T>
void AssignJson(const nlohmann::json& node, T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        if (!node.is_boolean()) {
            throw std::runtime_error("expects a boolean");
        }
        value = node.get<bool>();
    } else if constexpr (std::is_integral_v<T>) {
        if (node.is_number_unsigned()) {
            const uint64_t parsed = node.get<uint64_t>();
            if (!std::in_range<T>(parsed)) {
                throw std::runtime_error("integer is out of range");
            }
            value = static_cast<T>(parsed);
        } else if (node.is_number_integer()) {
            const int64_t parsed = node.get<int64_t>();
            if (!std::in_range<T>(parsed)) {
                throw std::runtime_error("integer is out of range");
            }
            value = static_cast<T>(parsed);
        } else {
            throw std::runtime_error("expects an integer");
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        if (!node.is_number()) {
            throw std::runtime_error("expects a number");
        }
        const T parsed = node.get<T>();
        if (!std::isfinite(parsed)) {
            throw std::runtime_error("expects a finite number");
        }
        value = parsed;
    } else if constexpr (std::is_enum_v<T>) {
        // 名前の表を持たない列挙は基底の整数。範囲外の数は誤り。
        std::underlying_type_t<T> parsed{};
        AssignJson(node, parsed);
        value = static_cast<T>(parsed);
    } else if constexpr (kIsVector<T>) {
        AssignVector(node, value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (!node.is_string()) {
            throw std::runtime_error("expects a string");
        }
        value = node.get<std::string>();
    } else {
        // nlohmann が変換できる他の型（物体参照など）。形が違えば nlohmann の例外。
        value = node.get<T>();
    }
}

template <class T>
void ReadStrict(const nlohmann::json& node, const char* key, T& value) {
    try {
        AssignJson(node, value);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(key) + ": " + error.what());
    }
}

template <class T>
void Clamp(T& value, const T& fallback, Range range) {
    if constexpr (kIsVector<T>) {
        Clamp(value.x, fallback.x, range);
        Clamp(value.y, fallback.y, range);
        if constexpr (!std::is_same_v<T, MATH::Vec2>) {
            Clamp(value.z, fallback.z, range);
        }
        if constexpr (std::is_same_v<T, MATH::Vec4>) {
            Clamp(value.w, fallback.w, range);
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        value =
            std::isfinite(value) ? std::clamp(value, static_cast<T>(range.min), static_cast<T>(range.max)) : fallback;
    } else {
        value = std::clamp(value, static_cast<T>(range.min), static_cast<T>(range.max));
    }
}

template <class T>
struct Writer {
    nlohmann::json& out;
    const T& object;

    template <class C, class M, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, M C::* member, [[maybe_unused]] const Options&... options) const {
        if constexpr ((kIsElements<Options> || ...)) {
            nlohmann::json list = nlohmann::json::array();
            for (const auto& element : object.*member) {
                nlohmann::json item = nlohmann::json::object();
                JSONFIELDS::Write(item, element, SubFields(options...));
                list.push_back(std::move(item));
            }
            out[key] = std::move(list);
        } else if constexpr ((kIsNested<Options> || ...)) {
            out[key] = nlohmann::json::object();
            JSONFIELDS::Write(out[key], object.*member, SubFields(options...));
        } else {
            CheckObjectRef<M, Options...>();
            out[key] = ToJson(Unwrap(object.*member));
        }
    }
    template <class C, class E, std::size_t N, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, E C::* member, const TEXT::EnumName<E> (&names)[N], const Options&...) const {
        out[key] = TEXT::EnumToName(names, object.*member);
    }
};

template <class T, bool Lenient>
struct Reader;

// 入れ子の構造体 1 つを読む。厳密な読みは失敗を項目のパス付きで投げ、緩い読みは object でない値を無視して false を返す。
template <bool Lenient, class S, class Fields>
bool ReadStruct(const nlohmann::json& node, const std::string& path, S& object, const Fields& fields) {
    if (!node.is_object()) {
        if constexpr (Lenient) {
            return false;
        } else {
            throw std::runtime_error(path + ": expects an object");
        }
    }
    if constexpr (Lenient) {
        fields(Reader<S, true>{node, object});
    } else {
        try {
            fields(Reader<S, false>{node, object});
        } catch (const std::exception& error) {
            throw std::runtime_error(path + "." + error.what());
        }
    }
    return true;
}

// 固定長の配列（std::array）は要素をその場で読む。先頭から読んだ分だけが置き換わり、足りない要素と、要素の中で欠けた項目は
// 現在値のまま。長さを超える要素は厳密な読みでは誤り、緩い読みでは無視する。
template <bool Lenient, class E, std::size_t N, class Fields>
void ReadList(const nlohmann::json& node, const char* key, std::array<E, N>& list, const Fields& fields) {
    if (!node.is_array()) {
        if constexpr (Lenient) {
            return;
        } else {
            throw std::runtime_error(std::string(key) + ": expects an array");
        }
    }
    if constexpr (!Lenient) {
        if (node.size() > N) {
            throw std::runtime_error(std::string(key) + ": expects at most " + std::to_string(N) + " elements");
        }
    }
    std::array<E, N> decoded = list;
    for (size_t index = 0; index < (std::min)(node.size(), N); ++index) {
        (void)ReadStruct<Lenient>(node[index], std::string(key) + "[" + std::to_string(index) + "]", decoded[index],
                                  fields);
    }
    list = decoded;
}

// 構造体の配列を読んで置き換える。緩い読みは配列でない値と object でない要素を無視する。
template <bool Lenient, class E, class Fields>
void ReadList(const nlohmann::json& node, const char* key, std::vector<E>& list, const Fields& fields) {
    if (!node.is_array()) {
        if constexpr (Lenient) {
            return;
        } else {
            throw std::runtime_error(std::string(key) + ": expects an array");
        }
    }
    std::vector<E> decoded;
    decoded.reserve(node.size());
    for (size_t index = 0; index < node.size(); ++index) {
        E element{};
        if (ReadStruct<Lenient>(node[index], std::string(key) + "[" + std::to_string(index) + "]", element, fields)) {
            decoded.push_back(std::move(element));
        }
    }
    list = std::move(decoded);
}

template <class T, bool Lenient>
struct Reader {
    const nlohmann::json& in;
    T& object;

    template <class C, class M, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, M C::* member, [[maybe_unused]] const Options&... options) const {
        [[maybe_unused]] const nlohmann::json* node = JSONREAD::FindMember(in, key);
        // 厳密な読みでは null は「指定なし」で、現在値のまま。
        if constexpr (!Lenient) {
            if (node != nullptr && node->is_null()) {
                node = nullptr;
            }
        }
        CheckObjectRef<M, Options...>();
        if constexpr ((kIsElements<Options> || ...)) {
            if (node != nullptr) {
                ReadList<Lenient>(*node, key, object.*member, SubFields(options...));
            }
        } else if constexpr ((kIsNested<Options> || ...)) {
            if (node != nullptr) {
                (void)ReadStruct<Lenient>(*node, key, object.*member, SubFields(options...));
            }
        } else if constexpr (Lenient) {
            ReadLenient(in, key, Unwrap(object.*member));
        } else if (node != nullptr) {
            ReadStrict(*node, key, Unwrap(object.*member));
        }
    }
    template <class C, class E, std::size_t N, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, E C::* member, const TEXT::EnumName<E> (&names)[N], const Options&...) const {
        const nlohmann::json* node = JSONREAD::FindMember(in, key);
        if (node == nullptr || node->is_null()) {
            return;
        }
        if constexpr (Lenient) {
            if (node->is_string()) {
                object.*member = TEXT::ParseEnum(names, node->get<std::string>(), object.*member);
            }
        } else if (!node->is_string() || !TEXT::TryParseEnum(names, node->get<std::string>(), object.*member)) {
            throw std::runtime_error(std::string(key) + ": expects one of the listed names");
        }
    }
};

template <class T>
struct Sanitizer {
    T& object;
    const T& defaults;

    template <class C, class M, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char*, M C::* member, [[maybe_unused]] const Options&... options) const {
        if constexpr ((kIsElements<Options> || ...)) {
            for (auto& element : object.*member) {
                element = JSONFIELDS::Sanitize(std::move(element), SubFields(options...));
            }
        } else if constexpr ((kIsNested<Options> || ...)) {
            object.*member = JSONFIELDS::Sanitize(std::move(object.*member), SubFields(options...), defaults.*member);
        } else if constexpr ((std::is_same_v<Options, Range> || ...)) {
            Clamp(Unwrap(object.*member), Unwrap(defaults.*member), MetaOf(options...).range);
        }
    }
};

template <class T>
struct Describer {
    std::vector<FieldInfo>& infos;

    template <class... Options>
    FieldInfo Make(const char* key, FieldKind kind, const Options&... options) const {
        const Meta meta = MetaOf(options...);
        FieldInfo info;
        info.id = key;
        info.label = LabelOf(meta, key);
        info.help = meta.help;
        info.unit = meta.unit;
        info.group = meta.group;
        info.kind = kind;
        info.hasRange = meta.hasRange;
        info.minimum = meta.range.min;
        info.maximum = meta.range.max;
        info.slider = meta.hasRange && meta.range.slider;
        info.common = meta.common;
        info.advanced = meta.advanced;
        info.readOnly = meta.readOnly;
        info.hidden = meta.hidden;
        info.color = meta.color;
        info.hasShow = meta.hasShow;
        info.assetType = meta.assetType;
        return info;
    }

    template <class C, class M, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, M C::*, const Options&... options) const {
        FieldInfo info = Make(key, KindOf<M, Options...>(), options...);
        if constexpr ((kIsElements<Options> || ...)) {
            info.children = JSONFIELDS::Describe<typename M::value_type>(SubFields(options...));
        } else if constexpr ((kIsNested<Options> || ...)) {
            info.children = JSONFIELDS::Describe<M>(SubFields(options...));
        }
        infos.push_back(std::move(info));
    }
    template <class C, class E, std::size_t N, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, E C::*, const TEXT::EnumName<E> (&names)[N], const Options&... options) const {
        FieldInfo info = Make(key, FieldKind::Enum, options...);
        for (const TEXT::EnumName<E>& entry : names) {
            info.enumNames.emplace_back(entry.name);
        }
        infos.push_back(std::move(info));
    }
};

} // namespace DETAIL

template <class T, class Fields>
void Write(nlohmann::json& out, const T& object, const Fields& fields) {
    fields(DETAIL::Writer<T>{out, object});
}

// 厳密な読み取り。object は失敗した時に一部だけ書き換わっているので、呼び出し側は作業用の複製へ読む。
template <class T, class Fields>
void Read(const nlohmann::json& in, T& object, const Fields& fields) {
    if (!in.is_object()) {
        throw std::runtime_error("expects a JSON object");
    }
    fields(DETAIL::Reader<T, false>{in, object});
}

template <class T, class Fields>
void ReadLenient(const nlohmann::json& in, T& object, const Fields& fields) {
    fields(DETAIL::Reader<T, true>{in, object});
}

template <class T, class Fields>
T Sanitize(T object, const Fields& fields, const T& defaults) {
    fields(DETAIL::Sanitizer<T>{object, defaults});
    return object;
}

// 項目の静的な説明。型情報の登録時に 1 回作る（Elements / Nested の子項目も含む）。
template <class T, class Fields>
std::vector<FieldInfo> Describe(const Fields& fields) {
    std::vector<FieldInfo> infos;
    fields(DETAIL::Describer<T>{infos});
    return infos;
}

} // namespace HIKARI::JSONFIELDS
