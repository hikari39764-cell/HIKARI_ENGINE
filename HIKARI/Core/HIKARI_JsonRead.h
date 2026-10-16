#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include <json.hpp>

#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Math/HIKARI_ScalarMath.h"

namespace HIKARI::JSONREAD {

inline const nlohmann::json* FindMember(const nlohmann::json& object, const char* key) noexcept {
    if (!object.is_object() || key == nullptr) {
        return nullptr;
    }
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &(*found);
}

inline float NumberMemberOr(const nlohmann::json& object, const char* key, float fallback) {
    const nlohmann::json* value = FindMember(object, key);
    return value != nullptr && value->is_number() ? value->get<float>() : fallback;
}

// 数値を読み、非有限なら fallback にして [minimum, maximum] へ収める。
inline float ClampedNumberMemberOr(const nlohmann::json& object, const char* key, float fallback, float minimum,
                                   float maximum) {
    return MATH::ClampFinite(NumberMemberOr(object, key, fallback), fallback, minimum, maximum);
}

inline int IntegerMemberOr(const nlohmann::json& object, const char* key, int fallback) {
    const nlohmann::json* value = FindMember(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (value->is_number_unsigned()) {
        const uint64_t parsed = value->get<uint64_t>();
        return parsed <= static_cast<uint64_t>((std::numeric_limits<int>::max)()) ? static_cast<int>(parsed) : fallback;
    }
    if (!value->is_number_integer()) {
        return fallback;
    }
    const int64_t parsed = value->get<int64_t>();
    return parsed >= static_cast<int64_t>((std::numeric_limits<int>::min)()) &&
                   parsed <= static_cast<int64_t>((std::numeric_limits<int>::max)())
               ? static_cast<int>(parsed)
               : fallback;
}

inline uint32_t UnsignedMemberOr(const nlohmann::json& object, const char* key, uint32_t fallback) {
    const nlohmann::json* value = FindMember(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (value->is_number_unsigned()) {
        const uint64_t parsed = value->get<uint64_t>();
        return parsed <= static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)()) ? static_cast<uint32_t>(parsed)
                                                                                       : fallback;
    }
    if (!value->is_number_integer()) {
        return fallback;
    }
    const int64_t parsed = value->get<int64_t>();
    return parsed >= 0 && parsed <= static_cast<int64_t>((std::numeric_limits<uint32_t>::max)())
               ? static_cast<uint32_t>(parsed)
               : fallback;
}

inline bool BooleanMemberOr(const nlohmann::json& object, const char* key, bool fallback) {
    const nlohmann::json* value = FindMember(object, key);
    return value != nullptr && value->is_boolean() ? value->get<bool>() : fallback;
}

inline std::string StringMemberOr(const nlohmann::json& object, const char* key, std::string fallback) {
    const nlohmann::json* value = FindMember(object, key);
    return value != nullptr && value->is_string() ? value->get<std::string>() : std::move(fallback);
}

inline float NumberOr(const nlohmann::json& node, std::size_t index, float fallback) {
    if (!node.is_array() || node.size() <= index || !node[index].is_number()) {
        return fallback;
    }
    return node[index].get<float>();
}

inline MATH::Vec2 Vec2Or(const nlohmann::json& node, const MATH::Vec2& fallback) {
    MATH::Vec2 value = fallback;
    if (node.is_array()) {
        value.x = NumberOr(node, 0, value.x);
        value.y = NumberOr(node, 1, value.y);
    } else if (node.is_object()) {
        value.x = NumberMemberOr(node, "x", value.x);
        value.y = NumberMemberOr(node, "y", value.y);
    }
    return value;
}

inline MATH::Vec3 Vec3Or(const nlohmann::json& node, const MATH::Vec3& fallback) {
    MATH::Vec3 value = fallback;
    if (node.is_array()) {
        value.x = NumberOr(node, 0, value.x);
        value.y = NumberOr(node, 1, value.y);
        value.z = NumberOr(node, 2, value.z);
    } else if (node.is_object()) {
        value.x = NumberMemberOr(node, "x", value.x);
        value.y = NumberMemberOr(node, "y", value.y);
        value.z = NumberMemberOr(node, "z", value.z);
    }
    return value;
}

inline MATH::Vec4 Vec4Or(const nlohmann::json& node, const MATH::Vec4& fallback) {
    MATH::Vec4 value = fallback;
    if (node.is_array()) {
        value.x = NumberOr(node, 0, value.x);
        value.y = NumberOr(node, 1, value.y);
        value.z = NumberOr(node, 2, value.z);
        value.w = NumberOr(node, 3, value.w);
    } else if (node.is_object()) {
        value.x = NumberMemberOr(node, "x", value.x);
        value.y = NumberMemberOr(node, "y", value.y);
        value.z = NumberMemberOr(node, "z", value.z);
        value.w = NumberMemberOr(node, "w", value.w);
    }
    return value;
}

inline MATH::Vec3 Vec3MemberOr(const nlohmann::json& object, const char* key, const MATH::Vec3& fallback) {
    const nlohmann::json* value = FindMember(object, key);
    return value != nullptr ? Vec3Or(*value, fallback) : fallback;
}

// 列挙値を名前（別名可）で読む。
template <class E, std::size_t N>
E EnumOr(const nlohmann::json& node, const TEXT::EnumName<E> (&names)[N], E fallback) {
    return node.is_string() ? TEXT::ParseEnum(names, node.get<std::string>(), fallback) : fallback;
}

} // namespace HIKARI::JSONREAD
