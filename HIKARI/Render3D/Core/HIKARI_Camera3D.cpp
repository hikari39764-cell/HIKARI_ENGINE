#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI {

    void Camera3D::SetPerspective(float fovYRad, float aspect, float nearZ, float farZ) {
        fovYRad_ = fovYRad;
        aspect_ = aspect;
        nearZ_ = nearZ;
        farZ_ = farZ;
        proj_ = MATH::Mat4::PerspectiveFovRH_ZO(fovYRad, aspect, nearZ, farZ);
    }

    void Camera3D::SetLookAt(const MATH::Vec3& eye, const MATH::Vec3& target, const MATH::Vec3& up) {
        eye_ = eye;
        target_ = target;
        up_ = up;
        view_ = MATH::Mat4::LookAtRH(eye_, target_, up_);
    }

} // namespace HIKARI
