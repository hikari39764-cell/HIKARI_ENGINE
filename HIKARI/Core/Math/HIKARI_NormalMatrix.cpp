#include "Core/Math/HIKARI_NormalMatrix.h"

#include <cmath>

namespace HIKARI::MATH {

    Mat4 BuildNormalMatrixFromWorld(const Mat4& world) {
        const float a00 = world.m[0][0];
        const float a01 = world.m[1][0];
        const float a02 = world.m[2][0];
        const float a10 = world.m[0][1];
        const float a11 = world.m[1][1];
        const float a12 = world.m[2][1];
        const float a20 = world.m[0][2];
        const float a21 = world.m[1][2];
        const float a22 = world.m[2][2];

        const float determinant =
            a00 * (a11 * a22 - a12 * a21) -
            a01 * (a10 * a22 - a12 * a20) +
            a02 * (a10 * a21 - a11 * a20);
        if (std::abs(determinant) <= 1e-6f) {
            return Mat4::Identity();
        }

        const float inverseDeterminant = 1.0f / determinant;
        Mat4 normalMatrix = Mat4::Identity();
        normalMatrix.m[0][0] = (a11 * a22 - a12 * a21) * inverseDeterminant;
        normalMatrix.m[0][1] = (a02 * a21 - a01 * a22) * inverseDeterminant;
        normalMatrix.m[0][2] = (a01 * a12 - a02 * a11) * inverseDeterminant;
        normalMatrix.m[1][0] = (a12 * a20 - a10 * a22) * inverseDeterminant;
        normalMatrix.m[1][1] = (a00 * a22 - a02 * a20) * inverseDeterminant;
        normalMatrix.m[1][2] = (a02 * a10 - a00 * a12) * inverseDeterminant;
        normalMatrix.m[2][0] = (a10 * a21 - a11 * a20) * inverseDeterminant;
        normalMatrix.m[2][1] = (a01 * a20 - a00 * a21) * inverseDeterminant;
        normalMatrix.m[2][2] = (a00 * a11 - a01 * a10) * inverseDeterminant;
        return normalMatrix;
    }

} // namespace HIKARI::MATH
