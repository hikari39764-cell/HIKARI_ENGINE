#pragma once
#include <cmath>
#include <array>

namespace HIKARI::MATH {

    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;

        Vec2 operator+(const Vec2& r) const { return { x + r.x, y + r.y }; }
        Vec2 operator-(const Vec2& r) const { return { x - r.x, y - r.y }; }
        Vec2 operator*(float s) const { return { x * s, y * s }; }
    };

    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vec3 operator+(const Vec3& r) const { return { x + r.x, y + r.y, z + r.z }; }
        Vec3 operator-(const Vec3& r) const { return { x - r.x, y - r.y, z - r.z }; }
        Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
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

    float Dot(const Vec3& a, const Vec3& b);
    Vec3 Cross(const Vec3& a, const Vec3& b);
    float Length(const Vec3& v);
    Vec3 Normalize(const Vec3& v);
    Quat NormalizeQ(const Quat& q);
    Mat4 Inverse(const Mat4& value);
    bool DecomposeTRS(
        const Mat4& value,
        Vec3& outTranslation,
        Quat& outRotation,
        Vec3& outScale);
    // QuaternionをHIKARIのXYZ Euler角へ戻す。戻り値はラジアン。
    Vec3 EulerXYZFromQuat(const Quat& q);
    // SceneDocument / Inspector用の度数版。
    Vec3 EulerXYZDegreesFromQuat(const Quat& q);
    // Select the equivalent Euler representation nearest to the current editor value.
    Vec3 EulerXYZDegreesFromQuatNearest(const Quat& q, const Vec3& referenceDegrees);

    // Runtime sanity checks for convention consistency.
    bool RunMathConventionSelfCheck();

} // namespace HIKARI::MATH
