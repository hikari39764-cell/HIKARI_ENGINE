#include "Render3D/Core/HIKARI_Camera3D.h"

#include <algorithm>
#include <cmath>

namespace HIKARI {

void Camera3D::SetPerspective(float fovYRad, float aspect, float nearZ, float farZ) {
    constexpr float kMinimumFovYRad = 0.01f;
    constexpr float kMaximumFovYRad = 3.13f;
    constexpr float kMinimumAspect = 0.05f;
    constexpr float kMinimumNearZ = 0.001f;
    constexpr float kMinimumClipRange = 0.001f;

    fovYRad_ = std::isfinite(fovYRad) ? std::clamp(fovYRad, kMinimumFovYRad, kMaximumFovYRad) : kDefaultFovYRad;
    aspect_ = std::isfinite(aspect) ? (std::max)(aspect, kMinimumAspect) : 16.0f / 9.0f;
    nearZ_ = std::isfinite(nearZ) ? (std::max)(nearZ, kMinimumNearZ) : kDefaultNearZ;
    farZ_ = std::isfinite(farZ) ? (std::max)(farZ, nearZ_ + kMinimumClipRange)
                                : (std::max)(kDefaultFarZ, nearZ_ + kMinimumClipRange);
    proj_ = MATH::Mat4::PerspectiveFovRH_ZO(fovYRad_, aspect_, nearZ_, farZ_);
    orthographic_ = false;
}

void Camera3D::SetOrthographic(float height, float aspect, float nearZ, float farZ) {
    // Share finite clipping/aspect policy with perspective lenses.
    SetPerspective(fovYRad_, aspect, nearZ, farZ);
    orthographicHeight_ = std::isfinite(height) ? std::clamp(height, 0.01f, 100000.0f) : 10.0f;
    orthographic_ = true;
    proj_ = MATH::Mat4::OrthoRH_ZO(orthographicHeight_ * aspect_, orthographicHeight_, nearZ_, farZ_);
}
void Camera3D::SetOffCenter(float left, float right, float bottom, float top, float nearZ, float farZ,
                            bool orthographic) {
    const float width = (std::max)(right - left, 1.0e-6f), height = (std::max)(top - bottom, 1.0e-6f);
    // 範囲の決まり（近い面・遠い面の下限）は対称の投影と同じにそろえる。
    SetPerspective(2.0f * std::atan(0.5f * height / (std::max)(nearZ, 0.001f)), width / height, nearZ, farZ);
    orthographic_ = orthographic;
    orthographicHeight_ = height;
    MATH::Mat4 m{};
    if (orthographic) {
        m = MATH::Mat4::Identity();
        m.m[0][0] = 2.0f / width;
        m.m[1][1] = 2.0f / height;
        m.m[3][0] = -(right + left) / width;
        m.m[3][1] = -(top + bottom) / height;
        m.m[2][2] = 1.0f / (nearZ_ - farZ_);
        m.m[3][2] = nearZ_ / (nearZ_ - farZ_);
    } else {
        m.m[0][0] = 2.0f * nearZ_ / width;
        m.m[1][1] = 2.0f * nearZ_ / height;
        m.m[2][0] = (right + left) / width;
        m.m[2][1] = (top + bottom) / height;
        m.m[2][2] = farZ_ / (nearZ_ - farZ_);
        m.m[2][3] = -1.0f;
        m.m[3][2] = (nearZ_ * farZ_) / (nearZ_ - farZ_);
    }
    proj_ = m;
}

void Camera3D::SetAspect(float aspect) {
    if (orthographic_)
        SetOrthographic(orthographicHeight_, aspect, nearZ_, farZ_);
    else
        SetPerspective(fovYRad_, aspect, nearZ_, farZ_);
}

void Camera3D::SetLookAt(const MATH::Vec3& eye, const MATH::Vec3& target, const MATH::Vec3& up) {
    eye_ = eye;
    target_ = target;
    up_ = up;
    view_ = MATH::Mat4::LookAtRH(eye_, target_, up_);
}

void Camera3D::SetDepthOfFieldSettings(const CAMERA::DepthOfFieldSettings& settings) {
    depthOfFieldSettings_ = CAMERA::NormalizeDepthOfFieldSettings(settings);
}

} // namespace HIKARI
