#include "Render3D/Lighting/HIKARI_LocalLightFrame.h"

#include <algorithm>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::RENDER3D::LIGHTING {

    LocalLightFrame BuildLocalLightFrame(
        const SceneEnvironment& environment) {

        LocalLightFrame frame{};
        frame.sourceCount =
            static_cast<uint32_t>(environment.pointLights.size());

        for (const PointLight& pointLight : environment.pointLights) {
            if (!pointLight.enabled || pointLight.range <= 0.0f) {
                continue;
            }

            ++frame.uploadableCount;
            if (frame.uploadedCount >= kLocalLightFrameCapacity) {
                continue;
            }

            LocalLightGpuData& light =
                frame.lights[frame.uploadedCount];
            light.positionRange = {
                pointLight.position.x,
                pointLight.position.y,
                pointLight.position.z,
                pointLight.range
            };
            light.colorIntensity = {
                pointLight.color.x,
                pointLight.color.y,
                pointLight.color.z,
                (std::max)(0.0f, pointLight.intensity)
            };
            light.directionInnerCos = {
                0.0f,
                -1.0f,
                0.0f,
                1.0f
            };
            light.outerCosFalloffTypeFlags = {
                -1.0f,
                1.0f,
                static_cast<float>(LocalLightType::Point),
                0.0f
            };
            ++frame.uploadedCount;
        }

        frame.clampedCount =
            frame.uploadableCount > frame.uploadedCount
            ? frame.uploadableCount - frame.uploadedCount
            : 0u;
        return frame;
    }

} // namespace HIKARI::RENDER3D::LIGHTING
