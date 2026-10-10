#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include <json.hpp>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Math/HIKARI_Math3D.h"

// 設定構造体のフィールドを 1 か所で宣言し、JSON 読み書きと値の正規化を共有する。
//
//   constexpr auto kFogFields = [](auto&& field) {
//       field("enabled", &FogSettings::enabled);
//       field("density", &FogSettings::density, JSONFIELDS::Range{0.0, 1.0});
//       field("mode", &FogSettings::mode, kFogModeNames);
//   };
//   JSONFIELDS::Write(out, fog, kFogFields);
//   JSONFIELDS::Read(in, fog, kFogFields);            // json.value(key, 現在値) と同じ規則
//   JSONFIELDS::ReadLenient(in, fog, kFogFields);     // 型不一致は現在値を保持（例外なし）
//   fog = JSONFIELDS::Sanitize(fog, kFogFields);      // Range: 非有限値は既定値、範囲外は丸め（Vec は成分ごと）
//
// Slider(min, max) は Range と同じ範囲で、編集 UI では滑块で出す（全域に意味がある強さ・混合・角度など）。
//
// Vec2/3/4 は配列で保存し、読み込みは配列と {x,y,z,w} の両方を受け付ける。
namespace HIKARI::JSONFIELDS {

struct Range {
    double min;
    double max;
    bool slider = false;
};

constexpr Range Slider(double min, double max) { return {min, max, true}; }

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
constexpr bool kIsVector =
    std::is_same_v<T, MATH::Vec2> || std::is_same_v<T, MATH::Vec3> || std::is_same_v<T, MATH::Vec4>;

// json.value(key, current) と同じ規則（型が合わなければ nlohmann の例外）
template <class T>
void ReadExact(const nlohmann::json& in, const char* key, T& value) {
    if constexpr (kIsVector<T>) {
        ReadVector(in, key, value);
    } else {
        value = in.value(key, value);
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
    } else {
        value = JSONREAD::StringMemberOr(in, key, std::move(value));
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

    template <class M, class... Options>
    void operator()(const char* key, M T::* member, const Options&...) const {
        out[key] = ToJson(object.*member);
    }
    template <class E, std::size_t N, class... Options>
    void operator()(const char* key, E T::* member, const TEXT::EnumName<E> (&names)[N], const Options&...) const {
        out[key] = TEXT::EnumToName(names, object.*member);
    }
};

template <class T, bool Lenient>
struct Reader {
    const nlohmann::json& in;
    T& object;

    template <class M, class... Options>
    void operator()(const char* key, M T::* member, const Options&...) const {
        if constexpr (Lenient) {
            ReadLenient(in, key, object.*member);
        } else {
            ReadExact(in, key, object.*member);
        }
    }
    template <class E, std::size_t N, class... Options>
    void operator()(const char* key, E T::* member, const TEXT::EnumName<E> (&names)[N], const Options&...) const {
        if (const nlohmann::json* node = JSONREAD::FindMember(in, key); node != nullptr && node->is_string()) {
            object.*member = TEXT::ParseEnum(names, node->get<std::string>(), object.*member);
        }
    }
};

template <class T>
struct Sanitizer {
    T& object;
    const T& defaults;

    template <class M, class... Options>
    void operator()(const char*, M T::*, const Options&...) const {}
    template <class M>
    void operator()(const char*, M T::* member, Range range) const {
        Clamp(object.*member, defaults.*member, range);
    }
};

} // namespace DETAIL

template <class T, class Fields>
void Write(nlohmann::json& out, const T& object, const Fields& fields) {
    fields(DETAIL::Writer<T>{out, object});
}

template <class T, class Fields>
void Read(const nlohmann::json& in, T& object, const Fields& fields) {
    fields(DETAIL::Reader<T, false>{in, object});
}

template <class T, class Fields>
void ReadLenient(const nlohmann::json& in, T& object, const Fields& fields) {
    fields(DETAIL::Reader<T, true>{in, object});
}

template <class T, class Fields>
T Sanitize(T object, const Fields& fields, const T& defaults = T{}) {
    fields(DETAIL::Sanitizer<T>{object, defaults});
    return object;
}

} // namespace HIKARI::JSONFIELDS
