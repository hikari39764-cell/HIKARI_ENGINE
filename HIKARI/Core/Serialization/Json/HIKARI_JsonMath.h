#pragma once

#include <DirectXMath.h>
#include <json.hpp>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::SERIALIZATION::JSON::MATH {

    inline nlohmann::json ToJsonArray(const ::HIKARI::MATH::Vec2& value) {

        return nlohmann::json::array({ value.x, value.y });
    }

    inline nlohmann::json ToJsonArray(const ::HIKARI::MATH::Vec3& value) {

        return nlohmann::json::array({ value.x, value.y, value.z });
    }

    inline nlohmann::json ToJsonArray(const ::HIKARI::MATH::Vec4& value) {

        return nlohmann::json::array({ value.x, value.y, value.z, value.w });
    }

    inline nlohmann::json ToJsonArray(const ::DirectX::XMFLOAT4& value) {

        return nlohmann::json::array({ value.x, value.y, value.z, value.w });
    }

} // namespace HIKARI::SERIALIZATION::JSON::MATH
