#pragma once
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

class Camera3D {
public:
    static constexpr float kDefaultFovYRad = 60.0f * 3.1415926535f / 180.0f;
    static constexpr float kDefaultNearZ = 0.1f;
    static constexpr float kDefaultFarZ = 2000.0f;

    void SetPerspective(float fovYRad, float aspect, float nearZ, float farZ);
    void SetOrthographic(float verticalSize, float aspect, float nearZ, float farZ);
    void SetAspect(float aspect);
    void SetLookAt(const MATH::Vec3& eye, const MATH::Vec3& target, const MATH::Vec3& up = {0.0f, 1.0f, 0.0f});

    const MATH::Mat4& GetView() const { return view_; }
    const MATH::Mat4& GetProj() const { return proj_; }
    MATH::Mat4 GetViewProj() const { return proj_ * view_; }
    MATH::Vec3 GetPosition() const { return eye_; }
    MATH::Vec3 GetTarget() const { return target_; }
    MATH::Vec3 GetUp() const { return up_; }
    float GetFovYRad() const { return fovYRad_; }
    float GetAspect() const { return aspect_; }
    float GetNearZ() const { return nearZ_; }
    float GetFarZ() const { return farZ_; }
    bool IsOrthographic() const { return orthographic_; }
    float GetOrthographicHeight() const { return orthographicHeight_; }

private:
    MATH::Vec3 eye_{0.0f, 0.0f, -5.0f};
    MATH::Vec3 target_{0.0f, 0.0f, 0.0f};
    MATH::Vec3 up_{0.0f, 1.0f, 0.0f};
    float fovYRad_ = kDefaultFovYRad;
    float aspect_ = 16.0f / 9.0f;
    float nearZ_ = kDefaultNearZ;
    float farZ_ = kDefaultFarZ;
    bool orthographic_ = false;
    float orthographicHeight_ = 10.0f;

    MATH::Mat4 view_ = MATH::Mat4::Identity();
    MATH::Mat4 proj_ = MATH::Mat4::Identity();
};

} // namespace HIKARI
