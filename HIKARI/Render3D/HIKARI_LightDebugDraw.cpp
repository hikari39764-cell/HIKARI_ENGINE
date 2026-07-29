#include "HIKARI_LightDebugDraw.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Lighting/HIKARI_LocalLightFrame.h"
#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::LIGHTDEBUGDRAW {

    namespace {
        unsigned int ToDebugColor(const MATH::Vec3& color, unsigned int alpha) {
            const auto toByte = [](float value) -> unsigned int {
                return static_cast<unsigned int>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
            };
            return (toByte(color.x) << 24) |
                (toByte(color.y) << 16) |
                (toByte(color.z) << 8) |
                (alpha & 0xFFu);
        }

        void SubmitLightMarker(
            const MATH::Vec3& position,
            const MATH::Vec3& color) {

            RENDERER3D::DEBUG::Box3D marker{};
            marker.transform.position = position;
            marker.size = { 0.12f, 0.12f, 0.12f };
            marker.outlineRgba = ToDebugColor(color, 0xFFu);
            marker.fillRgba = ToDebugColor(color, 0x28u);
            marker.outlineThicknessPixels = 2.0f;
            RENDERER3D::DEBUG::SubmitBox3D(marker);
        }

    }

    void SubmitDirectionalLightArrow(const MATH::Vec3& directionalDir, const SceneEnvironment& environment) {
        if (!environment.showLightDebug || !environment.directional.enabled) {
            return;
        }

        constexpr float kArrowLength = 2.5f;
        constexpr float kHeadLength = 0.35f;
        constexpr float kHeadHalfWidth = 0.12f;
        constexpr float kMarkerSize = 0.12f;
        const unsigned int lightColor = ToDebugColor(environment.directional.color, 0xFFu);

        MATH::Vec3 dir = MATH::Normalize(directionalDir);
        if (MATH::Length(dir) <= 1e-6f) {
            dir = { 0.0f, -1.0f, 0.0f };
        }

        const MATH::Vec3 origin{ 0.0f, 1.5f, 0.0f };
        const MATH::Vec3 head = origin + (dir * kArrowLength);

        RENDERER3D::DEBUG::SubmitStroke3D({
            origin, head, lightColor,
            RENDERER3D::DEBUG::DebugDepthMode::DepthTest,
            2.0f
        });

        MATH::Vec3 right = MATH::Cross(dir, { 0.0f, 1.0f, 0.0f });
        if (MATH::Length(right) <= 1e-6f) {
            right = MATH::Cross(dir, { 1.0f, 0.0f, 0.0f });
        }
        right = MATH::Normalize(right);

        const MATH::Vec3 up = MATH::Normalize(MATH::Cross(right, dir));
        const MATH::Vec3 neck = head - (dir * kHeadLength);

        RENDERER3D::DEBUG::SubmitStroke3D({ head, neck + (right * kHeadHalfWidth), lightColor, RENDERER3D::DEBUG::DebugDepthMode::DepthTest, 2.0f });
        RENDERER3D::DEBUG::SubmitStroke3D({ head, neck - (right * kHeadHalfWidth), lightColor, RENDERER3D::DEBUG::DebugDepthMode::DepthTest, 2.0f });
        RENDERER3D::DEBUG::SubmitStroke3D({ head, neck + (up * kHeadHalfWidth), lightColor, RENDERER3D::DEBUG::DebugDepthMode::DepthTest, 2.0f });
        RENDERER3D::DEBUG::SubmitStroke3D({ head, neck - (up * kHeadHalfWidth), lightColor, RENDERER3D::DEBUG::DebugDepthMode::DepthTest, 2.0f });

        RENDERER3D::DEBUG::Box3D marker{};
        marker.transform.position = origin;
        marker.size = { kMarkerSize, kMarkerSize, kMarkerSize };
        marker.outlineRgba = lightColor;
        marker.fillRgba =
            ToDebugColor(environment.directional.color, 0x28u);
        marker.outlineThicknessPixels = 2.0f;
        RENDERER3D::DEBUG::SubmitBox3D(marker);
    }

    void SubmitLocalLightDebug(const SceneEnvironment& environment) {
        if (!environment.showLightDebug || !environment.showLocalLightMarkers) {
            return;
        }

        for (const PointLight& light : environment.pointLights) {
            if (!RENDER3D::LIGHTING::IsLocalLightUploadable(light)) {
                continue;
            }
            SubmitLightMarker(light.position, light.color);
        }
        for (const SpotLight& light : environment.spotLights) {
            if (!RENDER3D::LIGHTING::IsLocalLightUploadable(light)) {
                continue;
            }
            SubmitLightMarker(light.position, light.color);
            const MATH::Vec3 direction =
                RENDER3D::LIGHTING::ResolveSpotLightDirection(light);
            RENDERER3D::DEBUG::SubmitStroke3D({
                light.position,
                light.position + direction * 0.75f,
                ToDebugColor(light.color, 0xFFu),
                RENDERER3D::DEBUG::DebugDepthMode::DepthTest,
                2.0f
            });
        }

        const RENDER3D::LIGHTING::LocalLightFrame frame =
            RENDER3D::LIGHTING::BuildLocalLightFrame(environment);
        for (uint32_t index = 0u;
             index < frame.uploadedCount;
             ++index) {
            const RENDER3D::LIGHTING::LocalLightGpuData& light =
                frame.lights[index];
            const MATH::Vec3 color{
                light.colorIntensity.x,
                light.colorIntensity.y,
                light.colorIntensity.z
            };
            const auto type = static_cast<RENDER3D::LIGHTING::LocalLightType>(
                static_cast<uint32_t>(
                    std::round(light.outerCosFalloffTypeFlags.z)));
            if (type == RENDER3D::LIGHTING::LocalLightType::Point) {
                RENDERER3D::DEBUG::WireSphere3D sphere{};
                sphere.center = {
                    light.positionRange.x,
                    light.positionRange.y,
                    light.positionRange.z
                };
                sphere.radius = light.positionRange.w;
                sphere.rgba = ToDebugColor(color, 0x66u);
                RENDERER3D::DEBUG::SubmitWireSphere3D(sphere);
            } else if (type ==
                       RENDER3D::LIGHTING::LocalLightType::Spot) {
                RENDERER3D::DEBUG::WireCone3D outerCone{};
                outerCone.origin = {
                    light.positionRange.x,
                    light.positionRange.y,
                    light.positionRange.z
                };
                outerCone.direction = {
                    light.directionInnerCos.x,
                    light.directionInnerCos.y,
                    light.directionInnerCos.z
                };
                outerCone.length = light.positionRange.w;
                outerCone.angleDegrees =
                    std::acos(std::clamp(
                        light.outerCosFalloffTypeFlags.x,
                        0.0f,
                        1.0f)) *
                    180.0f /
                    std::numbers::pi_v<float>;
                outerCone.rgba = ToDebugColor(color, 0x66u);
                outerCone.drawRadials = true;
                RENDERER3D::DEBUG::SubmitWireCone3D(outerCone);

                RENDERER3D::DEBUG::WireCone3D innerCone =
                    outerCone;
                innerCone.angleDegrees =
                    std::acos(std::clamp(
                        light.directionInnerCos.w,
                        0.0f,
                        1.0f)) *
                    180.0f /
                    std::numbers::pi_v<float>;
                innerCone.rgba = ToDebugColor(color, 0x99u);
                innerCone.drawRadials = false;
                RENDERER3D::DEBUG::SubmitWireCone3D(innerCone);
            }
        }
    }

} // namespace HIKARI::LIGHTDEBUGDRAW
