#include "Render3D/HIKARI_Math3D.h"
#include "Core/HIKARI_MathConfig.h"

#include <algorithm>
#include <cassert>

namespace HIKARI::MATH {

    float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    Vec3 Cross(const Vec3& a, const Vec3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }

    Vec3 Normalize(const Vec3& v) {
        const float len = Length(v);
        if (len <= 1e-6f) { return { 0.0f, 0.0f, 0.0f }; }
        return { v.x / len, v.y / len, v.z / len };
    }

    Quat NormalizeQ(const Quat& q) {
        const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (len <= 1e-6f) {
            return Quat::Identity();
        }
        const float inv = 1.0f / len;
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }

    Mat4 Inverse(const Mat4& value) {
        float a[4][8]{};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                a[row][col] = value.m[col][row];
            }
            a[row][row + 4] = 1.0f;
        }

        // Gauss-Jordan elimination with partial pivoting.
        for (int col = 0; col < 4; ++col) {
            int pivot = col;
            float pivotAbs = std::abs(a[col][col]);
            for (int row = col + 1; row < 4; ++row) {
                const float candidate = std::abs(a[row][col]);
                if (candidate > pivotAbs) {
                    pivot = row;
                    pivotAbs = candidate;
                }
            }

            if (pivotAbs <= 1e-8f) {
                return Mat4::Identity();
            }

            if (pivot != col) {
                for (int i = 0; i < 8; ++i) {
                    std::swap(a[col][i], a[pivot][i]);
                }
            }

            const float invPivot = 1.0f / a[col][col];
            for (int i = 0; i < 8; ++i) {
                a[col][i] *= invPivot;
            }

            for (int row = 0; row < 4; ++row) {
                if (row == col) {
                    continue;
                }
                const float factor = a[row][col];
                for (int i = 0; i < 8; ++i) {
                    a[row][i] -= factor * a[col][i];
                }
            }
        }

        Mat4 out{};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                out.m[col][row] = a[row][col + 4];
            }
        }
        return out;
    }

    bool DecomposeTRS(
        const Mat4& value,
        Vec3& outTranslation,
        Quat& outRotation,
        Vec3& outScale) {

        outTranslation = {
            value.m[3][0],
            value.m[3][1],
            value.m[3][2]
        };
        Vec3 right{
            value.m[0][0],
            value.m[0][1],
            value.m[0][2]
        };
        Vec3 up{
            value.m[1][0],
            value.m[1][1],
            value.m[1][2]
        };
        Vec3 forward{
            value.m[2][0],
            value.m[2][1],
            value.m[2][2]
        };
        outScale = {
            Length(right),
            Length(up),
            Length(forward)
        };
        if (outScale.x <= 1.0e-6f ||
            outScale.y <= 1.0e-6f ||
            outScale.z <= 1.0e-6f) {
            outRotation = Quat::Identity();
            return false;
        }

        right = right * (1.0f / outScale.x);
        up = up * (1.0f / outScale.y);
        forward = forward * (1.0f / outScale.z);
        if (Dot(Cross(right, up), forward) < 0.0f) {
            outScale.x = -outScale.x;
            right = right * -1.0f;
        }

        const float m00 = right.x;
        const float m01 = up.x;
        const float m02 = forward.x;
        const float m10 = right.y;
        const float m11 = up.y;
        const float m12 = forward.y;
        const float m20 = right.z;
        const float m21 = up.z;
        const float m22 = forward.z;
        Quat rotation{};
        const float trace = m00 + m11 + m22;
        if (trace > 0.0f) {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            rotation.w = 0.25f * s;
            rotation.x = (m21 - m12) / s;
            rotation.y = (m02 - m20) / s;
            rotation.z = (m10 - m01) / s;
        } else if (m00 > m11 && m00 > m22) {
            const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            rotation.w = (m21 - m12) / s;
            rotation.x = 0.25f * s;
            rotation.y = (m01 + m10) / s;
            rotation.z = (m02 + m20) / s;
        } else if (m11 > m22) {
            const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            rotation.w = (m02 - m20) / s;
            rotation.x = (m01 + m10) / s;
            rotation.y = 0.25f * s;
            rotation.z = (m12 + m21) / s;
        } else {
            const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            rotation.w = (m10 - m01) / s;
            rotation.x = (m02 + m20) / s;
            rotation.y = (m12 + m21) / s;
            rotation.z = 0.25f * s;
        }
        outRotation = NormalizeQ(rotation);
        return true;
    }

    Quat Quat::FromEulerXYZ(float rx, float ry, float rz) {
        const float cx = std::cos(rx * 0.5f), sx = std::sin(rx * 0.5f);
        const float cy = std::cos(ry * 0.5f), sy = std::sin(ry * 0.5f);
        const float cz = std::cos(rz * 0.5f), sz = std::sin(rz * 0.5f);

        Quat q{};
        q.w = cx * cy * cz + sx * sy * sz;
        q.x = sx * cy * cz - cx * sy * sz;
        q.y = cx * sy * cz + sx * cy * sz;
        q.z = cx * cy * sz - sx * sy * cz;
        return q;
    }

    Vec3 EulerXYZFromQuat(const Quat& rotation) {
        const Quat q = NormalizeQ(rotation);

        const float sinXCosY = 2.0f * (q.w * q.x + q.y * q.z);
        const float cosXCosY = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
        const float x = std::atan2(sinXCosY, cosXCosY);

        const float sinY = std::clamp(2.0f * (q.w * q.y - q.z * q.x), -1.0f, 1.0f);
        const float y = std::asin(sinY);

        const float sinZCosY = 2.0f * (q.w * q.z + q.x * q.y);
        const float cosZCosY = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
        const float z = std::atan2(sinZCosY, cosZCosY);

        return { x, y, z };
    }

    Vec3 EulerXYZDegreesFromQuat(const Quat& q) {
        constexpr float kRadToDeg = 180.0f / 3.1415926535f;
        const Vec3 radians = EulerXYZFromQuat(q);
        return radians * kRadToDeg;
    }

    Vec3 EulerXYZDegreesFromQuatNearest(
        const Quat& q,
        const Vec3& referenceDegrees) {

        const auto wrapNear = [](float degrees, float reference) {
            return reference + std::remainder(degrees - reference, 360.0f);
        };
        const auto wrapCandidate = [&wrapNear](
            const Vec3& candidate,
            const Vec3& reference) {
            return Vec3{
                wrapNear(candidate.x, reference.x),
                wrapNear(candidate.y, reference.y),
                wrapNear(candidate.z, reference.z)
            };
        };
        const auto distanceSquared = [](const Vec3& a, const Vec3& b) {
            const Vec3 delta = a - b;
            return Dot(delta, delta);
        };

        const Vec3 canonical = EulerXYZDegreesFromQuat(q);
        const Vec3 alternate{
            canonical.x + 180.0f,
            180.0f - canonical.y,
            canonical.z + 180.0f
        };
        const Vec3 canonicalNear = wrapCandidate(canonical, referenceDegrees);
        const Vec3 alternateNear = wrapCandidate(alternate, referenceDegrees);
        return distanceSquared(alternateNear, referenceDegrees) <
                distanceSquared(canonicalNear, referenceDegrees)
            ? alternateNear
            : canonicalNear;
    }

    Mat4 Mat4::Identity() {
        Mat4 r{};
        r.m[0][0] = 1.0f; r.m[1][1] = 1.0f; r.m[2][2] = 1.0f; r.m[3][3] = 1.0f;
        return r;
    }

    Mat4 Mat4::Translate(const Vec3& t) {
        Mat4 r = Identity();
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }

    Mat4 Mat4::Scale(const Vec3& s) {
        Mat4 r{};
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        r.m[3][3] = 1.0f;
        return r;
    }

    Mat4 Mat4::Rotate(const Quat& q) {
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

    Mat4 Mat4::TRS(const Vec3& t, const Quat& r, const Vec3& s) {
        return Translate(t) * Rotate(r) * Scale(s);
    }

    Mat4 Mat4::LookAtRH(const Vec3& eye, const Vec3& target, const Vec3& up) {
        const Vec3 f = Normalize(target - eye);
        const Vec3 r = Normalize(Cross(up, f));
        const Vec3 u = Cross(f, r);

        Mat4 m = Identity();
        m.m[0][0] = r.x; m.m[0][1] = u.x; m.m[0][2] = -f.x;
        m.m[1][0] = r.y; m.m[1][1] = u.y; m.m[1][2] = -f.y;
        m.m[2][0] = r.z; m.m[2][1] = u.z; m.m[2][2] = -f.z;
        m.m[3][0] = -Dot(r, eye);
        m.m[3][1] = -Dot(u, eye);
        m.m[3][2] = Dot(f, eye);
        return m;
    }

    Mat4 Mat4::PerspectiveFovRH_ZO(float fovY, float aspect, float zNear, float zFar) {
        Mat4 m{};
        const float f = 1.0f / std::tan(fovY * 0.5f);
        m.m[0][0] = f / aspect;
        m.m[1][1] = f;
        m.m[2][2] = zFar / (zNear - zFar);
        m.m[2][3] = -1.0f;
        m.m[3][2] = (zNear * zFar) / (zNear - zFar);
        return m;
    }

    Mat4 Mat4::OrthoRH_ZO(float width, float height, float zNear, float zFar) {
        Mat4 m = Identity();
        m.m[0][0] = 2.0f / width;
        m.m[1][1] = 2.0f / height;
        m.m[2][2] = 1.0f / (zNear - zFar);
        m.m[3][2] = zNear / (zNear - zFar);
        return m;
    }

    Mat4 Mat4::operator*(const Mat4& rhs) const {
        Mat4 out{};
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                out.m[c][r] =
                    m[0][r] * rhs.m[c][0] +
                    m[1][r] * rhs.m[c][1] +
                    m[2][r] * rhs.m[c][2] +
                    m[3][r] * rhs.m[c][3];
            }
        }
        return out;
    }

    Vec4 Mat4::TransformPoint(const Vec4& v) const {
        Vec4 out{};
        out.x = m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w;
        out.y = m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w;
        out.z = m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w;
        out.w = m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w;
        return out;
    }

    bool RunMathConventionSelfCheck() {
        static bool alreadyChecked = false;
        static bool result = false;
        if (alreadyChecked) { return result; }
        alreadyChecked = true;

        assert(kRightHanded);
        assert(kNdcDepthZeroToOne);

        const Vec3 x{ 1.0f, 0.0f, 0.0f };
        const Vec3 y{ 0.0f, 1.0f, 0.0f };
        const Vec3 z = Cross(x, y);
        const bool rhOk = (std::abs(z.z - 1.0f) < 1e-4f);

        const Mat4 view = Mat4::LookAtRH({ 0.0f, 0.0f, -5.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
        const Mat4 proj = Mat4::PerspectiveFovRH_ZO(60.0f * 3.1415926535f / 180.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        const Vec4 clip = (proj * view).TransformPoint({ 0.0f, 0.0f, 0.0f, 1.0f });

        const float ndcZ = clip.z / clip.w;
        const bool depthOk = ndcZ >= 0.0f && ndcZ <= 1.0f;

        result = rhOk && depthOk;
        assert(result);
        return result;
    }

} // namespace HIKARI::MATH
