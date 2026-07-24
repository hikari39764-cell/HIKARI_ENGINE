// =============================
// File: Matrix3x3.cpp
// =============================
#include "Matrix3x3.h"

Matrix3x3 Matrix3x3::operator*(const Matrix3x3& other) const {
    Matrix3x3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] =
                m[i][0] * other.m[0][j] +
                m[i][1] * other.m[1][j] +
                m[i][2] * other.m[2][j];
        }
    }
    return r;
}

Matrix3x3 Matrix3x3::MakeIdentity() {
    Matrix3x3 r{};
    r.m[0][0] = 1.0f; r.m[0][1] = 0.0f; r.m[0][2] = 0.0f;
    r.m[1][0] = 0.0f; r.m[1][1] = 1.0f; r.m[1][2] = 0.0f;
    r.m[2][0] = 0.0f; r.m[2][1] = 0.0f; r.m[2][2] = 1.0f;
    return r;
}

Matrix3x3 Matrix3x3::MakeTranslate(float tx, float ty) {
    Matrix3x3 r = MakeIdentity();
    r.m[2][0] = tx;
    r.m[2][1] = ty;
    return r;
}

Matrix3x3 Matrix3x3::MakeScale(float sx, float sy) {
    Matrix3x3 r{};
    r.m[0][0] = sx;  r.m[0][1] = 0.0f; r.m[0][2] = 0.0f;
    r.m[1][0] = 0.0f; r.m[1][1] = sy;  r.m[1][2] = 0.0f;
    r.m[2][0] = 0.0f; r.m[2][1] = 0.0f; r.m[2][2] = 1.0f;
    return r;
}

Matrix3x3 Matrix3x3::MakeRotate(float radians) {
    float c = std::cos(radians);
    float s = std::sin(radians);

    Matrix3x3 r{};
    r.m[0][0] = c;   r.m[0][1] = s;   r.m[0][2] = 0.0f;
    r.m[1][0] = -s;  r.m[1][1] = c;   r.m[1][2] = 0.0f;
    r.m[2][0] = 0.0f; r.m[2][1] = 0.0f; r.m[2][2] = 1.0f;
    return r;
}

Matrix3x3 Matrix3x3::Shear(float shearX, float shearY)
{
    Matrix3x3 r{};
    r.m[0][0] = 1.0f;  r.m[0][1] = shearX; r.m[0][2] = 0.0f;
    r.m[1][0] = shearY; r.m[1][1] = 1.0f;  r.m[1][2] = 0.0f;
    r.m[2][0] = 0.0f;  r.m[2][1] = 0.0f;  r.m[2][2] = 1.0f;
    return r;
}
