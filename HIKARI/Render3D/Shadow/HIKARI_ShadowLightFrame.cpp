#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"

#include <algorithm>
#include <array>
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

            constexpr float kSnapTexels = 1.0f;
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

        MATH::Vec3 SafeCameraForward(const Camera3D& camera) {
            MATH::Vec3 forward =
                MATH::Normalize(camera.GetTarget() - camera.GetPosition());
            if (MATH::Length(forward) <= 1e-6f) {
                forward = { 0.0f, 0.0f, 1.0f };
            }
            return forward;
        }

        ShadowLightFrame BuildCascadeFrame(
            const SceneEnvironment& environment,
            const Camera3D& camera,
            uint32_t resolution,
            float splitNear,
            float splitFar) {

            const MATH::Vec3 cameraForward = SafeCameraForward(camera);
            MATH::Vec3 cameraRight =
                MATH::Normalize(MATH::Cross(cameraForward, camera.GetUp()));
            if (MATH::Length(cameraRight) <= 1e-6f) {
                cameraRight = { 1.0f, 0.0f, 0.0f };
            }
            const MATH::Vec3 cameraUp =
                MATH::Normalize(MATH::Cross(cameraRight, cameraForward));
            const float tanHalfFov =
                std::tan((std::max)(0.01f, camera.GetFovYRad()) * 0.5f);
            const float nearHalfHeight = splitNear * tanHalfFov;
            const float farHalfHeight = splitFar * tanHalfFov;
            const float nearHalfWidth = nearHalfHeight * camera.GetAspect();
            const float farHalfWidth = farHalfHeight * camera.GetAspect();
            const MATH::Vec3 nearCenter =
                camera.GetPosition() + cameraForward * splitNear;
            const MATH::Vec3 farCenter =
                camera.GetPosition() + cameraForward * splitFar;
            const std::array<MATH::Vec3, 8> corners = {
                nearCenter - cameraRight * nearHalfWidth - cameraUp * nearHalfHeight,
                nearCenter + cameraRight * nearHalfWidth - cameraUp * nearHalfHeight,
                nearCenter + cameraRight * nearHalfWidth + cameraUp * nearHalfHeight,
                nearCenter - cameraRight * nearHalfWidth + cameraUp * nearHalfHeight,
                farCenter - cameraRight * farHalfWidth - cameraUp * farHalfHeight,
                farCenter + cameraRight * farHalfWidth - cameraUp * farHalfHeight,
                farCenter + cameraRight * farHalfWidth + cameraUp * farHalfHeight,
                farCenter - cameraRight * farHalfWidth + cameraUp * farHalfHeight,
            };

            MATH::Vec3 center{};
            for (const MATH::Vec3& corner : corners) {
                center = center + corner;
            }
            center = center * (1.0f / static_cast<float>(corners.size()));

            float radius = 1.0f;
            for (const MATH::Vec3& corner : corners) {
                radius = (std::max)(radius, MATH::Length(corner - center));
            }
            radius = std::ceil(radius * 16.0f) / 16.0f;

            const MATH::Vec3 lightDir = ResolveLightDirection(environment);
            MATH::Vec3 up{ 0.0f, 1.0f, 0.0f };
            if (std::abs(MATH::Dot(lightDir, up)) > 0.95f) {
                up = { 1.0f, 0.0f, 0.0f };
            }
            MATH::Vec3 right = MATH::Normalize(MATH::Cross(up, lightDir));
            MATH::Vec3 actualUp = MATH::Normalize(MATH::Cross(lightDir, right));

            const float baseOrthoSize = radius * 2.0f;
            const float baseTexelWorldSize =
                baseOrthoSize /
                static_cast<float>((std::max)(1u, resolution));
            const float guardTexels =
                (std::max)(
                    2.0f,
                    std::ceil(std::clamp(
                        environment.directionalShadow.pcfRadius,
                        0.0f,
                        4.0f)) +
                        1.0f);
            // Snap による最大半 Texel の移動と PCF 範囲を含む Guard を確保し、
            // Frustum 端の Receiver が Cascade 外へ抜けないようにする。
            const float orthoSize =
                baseOrthoSize +
                baseTexelWorldSize * guardTexels * 2.0f;
            const float anchorGrid =
                ResolveShadowAnchorGrid(environment, orthoSize, resolution);
            MATH::Vec3 anchor = center;
            if (anchorGrid > 0.0f) {
                anchor =
                    right * SnapShadowAnchorValue(
                        MATH::Dot(center, right), anchorGrid) +
                    actualUp * SnapShadowAnchorValue(
                        MATH::Dot(center, actualUp), anchorGrid) +
                    lightDir * MATH::Dot(center, lightDir);
            }

            const float depthPadding =
                (std::max)(radius, environment.directionalShadow.farPlane * 0.25f);
            const float lightDistance = radius + depthPadding;
            const float nearPlane =
                (std::max)(0.01f, environment.directionalShadow.nearPlane);
            const float farPlane =
                (std::max)(nearPlane + 1.0f, lightDistance * 2.0f + radius);
            const MATH::Vec3 lightPos = anchor - lightDir * lightDistance;

            ShadowLightFrame frame{};
            frame.view = MATH::Mat4::LookAtRH(lightPos, anchor, actualUp);
            frame.viewProj =
                MATH::Mat4::OrthoRH_ZO(
                    orthoSize, orthoSize, nearPlane, farPlane) *
                frame.view;
            frame.anchor = anchor;
            frame.lightPosition = lightPos;
            frame.lightDirection = lightDir;
            frame.right = right;
            frame.up = actualUp;
            frame.anchorGrid = anchorGrid;
            frame.splitNear = splitNear;
            frame.splitFar = splitFar;
            frame.orthoSize = orthoSize;
            frame.lightNear = nearPlane;
            frame.lightFar = farPlane;
            return frame;
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

    DirectionalShadowFrameSet BuildDirectionalShadowFrameSet(
        const SceneEnvironment& environment,
        const Camera3D& camera,
        uint32_t cascadeResolution) {

        DirectionalShadowFrameSet result{};
        result.cascadeCount = std::clamp(
            environment.directionalShadow.cascadeCount,
            1u,
            kMaxDirectionalShadowCascades);
        const float nearDistance = (std::max)(0.01f, camera.GetNearZ());
        const float farDistance = (std::max)(
            nearDistance + 0.01f,
            (std::min)(
                camera.GetFarZ(),
                environment.directionalShadow.shadowDistance));
        const float lambda = std::clamp(
            environment.directionalShadow.cascadeSplitLambda,
            0.0f,
            1.0f);

        float splitNear = nearDistance;
        for (uint32_t cascadeIndex = 0;
             cascadeIndex < result.cascadeCount;
             ++cascadeIndex) {
            const float ratio =
                static_cast<float>(cascadeIndex + 1u) /
                static_cast<float>(result.cascadeCount);
            const float logarithmic =
                nearDistance * std::pow(farDistance / nearDistance, ratio);
            const float uniform =
                nearDistance + (farDistance - nearDistance) * ratio;
            const float splitFar =
                cascadeIndex + 1u == result.cascadeCount
                    ? farDistance
                    : uniform * (1.0f - lambda) + logarithmic * lambda;
            result.cascades[cascadeIndex] = BuildCascadeFrame(
                environment,
                camera,
                cascadeResolution,
                splitNear,
                splitFar);
            (&result.splitDistances.x)[cascadeIndex] = splitFar;
            splitNear = splitFar;
        }
        for (uint32_t cascadeIndex = result.cascadeCount;
             cascadeIndex < kMaxDirectionalShadowCascades;
             ++cascadeIndex) {
            result.cascades[cascadeIndex] =
                result.cascades[result.cascadeCount - 1u];
            (&result.splitDistances.x)[cascadeIndex] = farDistance;
        }
        return result;
    }

} // namespace HIKARI::SHADOW
