#pragma once

#include <cstddef>

#include <json.hpp>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::JSONREAD {

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
            value.x = node.value("x", value.x);
            value.y = node.value("y", value.y);
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
            value.x = node.value("x", value.x);
            value.y = node.value("y", value.y);
            value.z = node.value("z", value.z);
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
            value.x = node.value("x", value.x);
            value.y = node.value("y", value.y);
            value.z = node.value("z", value.z);
            value.w = node.value("w", value.w);
        }
        return value;
    }

} // namespace HIKARI::JSONREAD
