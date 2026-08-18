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

        fovYRad_ = std::isfinite(fovYRad)
            ? std::clamp(fovYRad, kMinimumFovYRad, kMaximumFovYRad)
            : kDefaultFovYRad;
        aspect_ = std::isfinite(aspect)
            ? (std::max)(aspect, kMinimumAspect)
            : 16.0f / 9.0f;
        nearZ_ = std::isfinite(nearZ)
            ? (std::max)(nearZ, kMinimumNearZ)
            : kDefaultNearZ;
        farZ_ = std::isfinite(farZ)
            ? (std::max)(farZ, nearZ_ + kMinimumClipRange)
            : (std::max)(kDefaultFarZ, nearZ_ + kMinimumClipRange);
        proj_ = MATH::Mat4::PerspectiveFovRH_ZO(
            fovYRad_, aspect_, nearZ_, farZ_);
    }

    void Camera3D::SetLookAt(const MATH::Vec3& eye, const MATH::Vec3& target, const MATH::Vec3& up) {
        eye_ = eye;
        target_ = target;
        up_ = up;
        view_ = MATH::Mat4::LookAtRH(eye_, target_, up_);
    }

} // namespace HIKARI
