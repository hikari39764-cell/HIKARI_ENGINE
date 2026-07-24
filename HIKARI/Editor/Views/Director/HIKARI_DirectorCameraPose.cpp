#include "Editor/Views/Director/HIKARI_DirectorCameraPose.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {

    DirectorCameraPose MakeDirectorCameraPose(
        const Camera3D& camera) {

        MATH::Vec3 forward =
            camera.GetTarget() - camera.GetPosition();
        if (MATH::Length(forward) <= 1.0e-5f) {
            forward = { 0.0f, 0.0f, 1.0f };
        } else {
            forward = MATH::Normalize(forward);
        }

        const float yaw = std::atan2(forward.x, forward.z);
        const float pitch = std::asin(
            std::clamp(forward.y, -1.0f, 1.0f));

        DirectorCameraPose pose{};
        pose.position = camera.GetPosition();
        pose.rotation =
            MATH::Quat::FromEulerXYZ(-pitch, yaw, 0.0f);
        return pose;
    }

} // namespace HIKARI::EDITOR
