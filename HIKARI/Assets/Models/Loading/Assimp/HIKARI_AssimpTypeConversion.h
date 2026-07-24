#pragma once

#include <string>

#include <assimp/scene.h>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"

namespace HIKARI::ASSETS::MODELS::ASSIMP {

    inline std::string ToString(const aiString& value) {
        return value.length > 0 ? std::string(value.C_Str()) : std::string{};
    }

    inline MATH::Vec3 ToVec3(const aiVector3D& value) {
        return { value.x, value.y, value.z };
    }

    inline MATH::Vec4 ToVec4(const aiColor4D& value) {
        return { value.r, value.g, value.b, value.a };
    }

    inline MATH::Mat4 ToMat4(const aiMatrix4x4& value) {
        MATH::Mat4 out = MATH::Mat4::Identity();
        out.m[0][0] = value.a1;
        out.m[1][0] = value.a2;
        out.m[2][0] = value.a3;
        out.m[3][0] = value.a4;

        out.m[0][1] = value.b1;
        out.m[1][1] = value.b2;
        out.m[2][1] = value.b3;
        out.m[3][1] = value.b4;

        out.m[0][2] = value.c1;
        out.m[1][2] = value.c2;
        out.m[2][2] = value.c3;
        out.m[3][2] = value.c4;

        out.m[0][3] = value.d1;
        out.m[1][3] = value.d2;
        out.m[2][3] = value.d3;
        out.m[3][3] = value.d4;
        return out;
    }

    inline MATH::Quat ToQuat(const aiQuaternion& value) {
        return MATH::NormalizeQ({ value.x, value.y, value.z, value.w });
    }

} // namespace HIKARI::ASSETS::MODELS::ASSIMP
