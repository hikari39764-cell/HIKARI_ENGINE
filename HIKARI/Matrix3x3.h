#pragma once
#include <cmath>

struct Matrix3x3 {
    float m[3][3];

    // multiply (this * other)
    Matrix3x3 operator*(const Matrix3x3& other) const;

    // static constructors
    static Matrix3x3 MakeIdentity();
    static Matrix3x3 MakeTranslate(float tx, float ty);
    static Matrix3x3 MakeScale(float sx, float sy);
    static Matrix3x3 MakeRotate(float radians);
    static Matrix3x3 Shear(float shearX, float shearY);
};
