#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::SHADOW {

    namespace {
        float SnapShadowAnchorValue(float value, float grid) {
            if (grid <= 0.0001f) {
                return value;
            }
            return std::round(value / grid) * grid;
        }

        float ResolveShadowAnchorGrid(
            const SceneEnvironment& environment,
            float orthoSize,
            uint32_t resolution) {

            if (!environment.directionalShadow.stabilize || resolution == 0u) {
                return 0.0f;
            }

            constexpr float kSnapTexels = 8.0f;
            const float texelWorldSize =
                orthoSize / static_cast<float>((std::max)(1u, resolution));
            return (std::max)(texelWorldSize * kSnapTexels, 0.0001f);
        }

        MATH::Vec3 ResolveLightDirection(const SceneEnvironment& environment) {
            MATH::Vec3 lightDir = MATH::Normalize(environment.directional.direction);
            if (MATH::Length(lightDir) <= 1e-6f) {
                lightDir = MATH::Normalize(MATH::Vec3{ 0.4f, -1.0f, -0.6f });
            }
            return lightDir;
        }
    }

    uint32_t ResolveShadowResolution(uint32_t resolution) {
        if (resolution <= 1024) {
            return 1024;
        }
        if (resolution <= 2048) {
            return 2048;
        }
        return 4096;
    }

    float ResolveShadowDepthSpan(
        const SceneEnvironment& environment,
        float orthoSize) {

        const float configuredFar =
            (std::max)(0.01f, environment.directionalShadow.farPlane);
        const float configuredDistance =
            (std::max)(1.0f, environment.directionalShadow.shadowDistance);
        return (std::max)(
            configuredFar,
            (std::max)(configuredDistance * 2.0f, orthoSize * 1.5f));
    }

    ShadowLightFrame BuildShadowLightFrame(
        const SceneEnvironment& environment,
        const Camera3D& camera,
        uint32_t resolution) {

        ShadowLightFrame frame{};
        const MATH::Vec3 lightDir = ResolveLightDirection(environment);
        MATH::Vec3 up{ 0.0f, 1.0f, 0.0f };
        if (std::abs(MATH::Dot(lightDir, up)) > 0.95f) {
            up = { 1.0f, 0.0f, 0.0f };
        }

        MATH::Vec3 right = MATH::Normalize(MATH::Cross(up, lightDir));
        if (MATH::Length(right) <= 1e-6f) {
            right = { 1.0f, 0.0f, 0.0f };
        }
        MATH::Vec3 actualUp = MATH::Normalize(MATH::Cross(lightDir, right));
        if (MATH::Length(actualUp) <= 1e-6f) {
            actualUp = up;
        }

        const float orthoSize =
            (std::max)(1.0f, environment.directionalShadow.orthoSize);
        const float nearPlane =
            (std::max)(0.001f, environment.directionalShadow.nearPlane);
        const float depthSpan = ResolveShadowDepthSpan(environment, orthoSize);
        const float farPlane = (std::max)(nearPlane + 0.01f, depthSpan);
        const float anchorGrid =
            ResolveShadowAnchorGrid(environment, orthoSize, resolution);

        const MATH::Vec3 cameraCenter = camera.GetPosition();
        MATH::Vec3 anchor = cameraCenter;
        if (anchorGrid > 0.0f) {
            const float depthAnchorGrid =
                (std::max)(anchorGrid * 32.0f, depthSpan / 16.0f);
            const float snappedX =
                SnapShadowAnchorValue(MATH::Dot(cameraCenter, right), anchorGrid);
            const float snappedY =
                SnapShadowAnchorValue(MATH::Dot(cameraCenter, actualUp), anchorGrid);
            const float snappedZ =
                SnapShadowAnchorValue(MATH::Dot(cameraCenter, lightDir), depthAnchorGrid);
            anchor =
                right * snappedX +
                actualUp * snappedY +
                lightDir * snappedZ;
        }

        const float lightDistance = (std::max)(1.0f, farPlane * 0.5f);
        const MATH::Vec3 lightPos = anchor - lightDir * lightDistance;
        frame.view = MATH::Mat4::LookAtRH(lightPos, anchor, actualUp);
        frame.viewProj =
            MATH::Mat4::OrthoRH_ZO(orthoSize, orthoSize, nearPlane, farPlane) *
            frame.view;
        frame.anchor = anchor;
        frame.lightPosition = lightPos;
        frame.lightDirection = lightDir;
        frame.right = right;
        frame.up = actualUp;
        frame.anchorGrid = anchorGrid;
        return frame;
    }

} // namespace HIKARI::SHADOW
