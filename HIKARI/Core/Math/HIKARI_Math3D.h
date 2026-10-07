#pragma once
#include <cmath>
#include <array>
#include <cstdint>

namespace HIKARI::MATH {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2 operator+(const Vec2& r) const { return {x + r.x, y + r.y}; }
    Vec2 operator-(const Vec2& r) const { return {x - r.x, y - r.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3 operator+(const Vec3& r) const { return {x + r.x, y + r.y, z + r.z}; }
    Vec3 operator-(const Vec3& r) const { return {x - r.x, y - r.y, z - r.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    static Quat Identity() { return {}; }
    static Quat FromEulerXYZ(float rx, float ry, float rz);
};

// 画面の右・上と注視方向。RH view の Z 軸は -forward。
struct ViewBasisRH {
    Vec3 right, up, forward;
};
ViewBasisRH BuildViewBasisRH(const Vec3& forward, const Vec3& up);

struct Mat4 {
    // Column-major storage: m[col][row]
    float m[4][4]{};

    static Mat4 Identity();
    static Mat4 Translate(const Vec3& t);
    static Mat4 Scale(const Vec3& s);
    static Mat4 Rotate(const Quat& q);
    static Mat4 TRS(const Vec3& t, const Quat& r, const Vec3& s);
    static Mat4 LookAtRH(const Vec3& eye, const Vec3& target, const Vec3& up);
    static Mat4 PerspectiveFovRH_ZO(float fovY, float aspect, float zNear, float zFar);
    static Mat4 OrthoRH_ZO(float width, float height, float zNear, float zFar);

    Mat4 operator*(const Mat4& rhs) const;
    Vec4 TransformPoint(const Vec4& v) const;
};

// 変換・カリング・アニメーションの毎フレーム経路で呼ばれる小関数は、
// 翻訳単位をまたいでも LTCG に頼らず展開されるようヘッダーで定義する。
inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }

inline Vec3 Normalize(const Vec3& v) {
    const float len = Length(v);
    if (len <= 1e-6f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

inline Quat NormalizeQ(const Quat& q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len <= 1e-6f) {
        return Quat::Identity();
    }
    const float inv = 1.0f / len;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

inline Mat4 Mat4::Identity() {
    Mat4 r{};
    r.m[0][0] = 1.0f;
    r.m[1][1] = 1.0f;
    r.m[2][2] = 1.0f;
    r.m[3][3] = 1.0f;
    return r;
}

inline Mat4 Mat4::Translate(const Vec3& t) {
    Mat4 r = Identity();
    r.m[3][0] = t.x;
    r.m[3][1] = t.y;
    r.m[3][2] = t.z;
    return r;
}

inline Mat4 Mat4::Scale(const Vec3& s) {
    Mat4 r{};
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    r.m[3][3] = 1.0f;
    return r;
}

inline Mat4 Mat4::Rotate(const Quat& q) {
    Mat4 r = Identity();
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;

    r.m[0][0] = 1.0f - 2.0f * (yy + zz);
    r.m[0][1] = 2.0f * (xy + wz);
    r.m[0][2] = 2.0f * (xz - wy);

    r.m[1][0] = 2.0f * (xy - wz);
    r.m[1][1] = 1.0f - 2.0f * (xx + zz);
    r.m[1][2] = 2.0f * (yz + wx);

    r.m[2][0] = 2.0f * (xz + wy);
    r.m[2][1] = 2.0f * (yz - wx);
    r.m[2][2] = 1.0f - 2.0f * (xx + yy);
    return r;
}

inline Mat4 Mat4::operator*(const Mat4& rhs) const {
    Mat4 out{};
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out.m[c][r] = m[0][r] * rhs.m[c][0] + m[1][r] * rhs.m[c][1] + m[2][r] * rhs.m[c][2] + m[3][r] * rhs.m[c][3];
        }
    }
    return out;
}

inline Mat4 Mat4::TRS(const Vec3& t, const Quat& r, const Vec3& s) { return Translate(t) * Rotate(r) * Scale(s); }

inline Vec4 Mat4::TransformPoint(const Vec4& v) const {
    Vec4 out{};
    out.x = m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w;
    out.y = m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w;
    out.z = m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w;
    out.w = m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w;
    return out;
}

// DirectX cubemap の +X,-X,+Y,-Y,+Z,-Z と [0,1] UV。
Vec3 CubeFaceDirection(uint32_t face, float u, float v);
void DirectionToCubeFaceUv(const Vec3& direction, uint32_t& face, float& u, float& v);
Vec3 DecodeOctahedralDirection(const Vec2& uv);
Mat4 Inverse(const Mat4& value);
bool DecomposeTRS(const Mat4& value, Vec3& outTranslation, Quat& outRotation, Vec3& outScale);
// QuaternionをHIKARIのXYZ Euler角へ戻す。戻り値はラジアン。
Vec3 EulerXYZFromQuat(const Quat& q);
// SceneDocument / Inspector用の度数版。
Vec3 EulerXYZDegreesFromQuat(const Quat& q);
// Select the equivalent Euler representation nearest to the current editor value.
Vec3 EulerXYZDegreesFromQuatNearest(const Quat& q, const Vec3& referenceDegrees);

// Runtime sanity checks for convention consistency.
bool RunMathConventionSelfCheck();

} // namespace HIKARI::MATH
