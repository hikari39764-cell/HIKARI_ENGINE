#include "Render3D/Lighting/HIKARI_LocalLightFrame.h"

#include <algorithm>

#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::RENDER3D::LIGHTING {

    namespace {

        struct LocalLightCandidate {
            LocalLightGpuData gpu{};
            LocalLightType type = LocalLightType::Point;
            int priority = 0;
        };

        LocalLightCandidate BuildPointCandidate(
            const PointLight& pointLight) {

            LocalLightCandidate candidate{};
            candidate.type = LocalLightType::Point;
            candidate.priority = pointLight.priority;
            candidate.gpu.positionRange = {
                pointLight.position.x,
                pointLight.position.y,
                pointLight.position.z,
                pointLight.range
            };
            candidate.gpu.colorIntensity = {
                pointLight.color.x,
                pointLight.color.y,
                pointLight.color.z,
                (std::max)(0.0f, pointLight.intensity)
            };
            candidate.gpu.directionInnerCos = {
                0.0f,
                -1.0f,
                0.0f,
                1.0f
            };
            candidate.gpu.outerCosFalloffTypeFlags = {
                -1.0f,
                1.0f,
                static_cast<float>(LocalLightType::Point),
                0.0f
            };
            return candidate;
        }

        LocalLightCandidate BuildSpotCandidate(
            const SpotLight& spotLight) {

            const MATH::Vec3 direction =
                ResolveSpotLightDirection(spotLight);
            const SpotLightCone cone =
                ResolveSpotLightCone(spotLight);

            LocalLightCandidate candidate{};
            candidate.type = LocalLightType::Spot;
            candidate.priority = spotLight.priority;
            candidate.gpu.positionRange = {
                spotLight.position.x,
                spotLight.position.y,
                spotLight.position.z,
                spotLight.range
            };
            candidate.gpu.colorIntensity = {
                spotLight.color.x,
                spotLight.color.y,
                spotLight.color.z,
                (std::max)(0.0f, spotLight.intensity)
            };
            candidate.gpu.directionInnerCos = {
                direction.x,
                direction.y,
                direction.z,
                cone.innerCos
            };
            candidate.gpu.outerCosFalloffTypeFlags = {
                cone.outerCos,
                cone.angularFalloff,
                static_cast<float>(LocalLightType::Spot),
                0.0f
            };
            return candidate;
        }

        void InsertLocalLightCandidate(
            std::array<
                LocalLightCandidate,
                kLocalLightFrameCapacity>& selected,
            uint32_t& selectedCount,
            const LocalLightCandidate& candidate) {

            // 固定 GPU 予算の範囲で上位 priority のみを安定選択する。
            // 同一 priority は Scene authoring 順を維持し、毎フレームの
            // temporary allocation を発生させない。
            uint32_t insertionIndex = 0u;
            while (insertionIndex < selectedCount &&
                   selected[insertionIndex].priority >=
                       candidate.priority) {
                ++insertionIndex;
            }
            if (insertionIndex >= kLocalLightFrameCapacity) {
                return;
            }

            const uint32_t nextCount = (std::min)(
                selectedCount + 1u,
                kLocalLightFrameCapacity);
            for (uint32_t index = nextCount - 1u;
                 index > insertionIndex;
                 --index) {
                selected[index] = selected[index - 1u];
            }
            selected[insertionIndex] = candidate;
            selectedCount = nextCount;
        }

    } // namespace

    LocalLightFrame BuildLocalLightFrame(
        const SceneEnvironment& environment) {

        LocalLightFrame frame{};
        frame.sourcePointCount =
            static_cast<uint32_t>(environment.pointLights.size());
        frame.sourceSpotCount =
            static_cast<uint32_t>(environment.spotLights.size());
        frame.sourceCount =
            frame.sourcePointCount + frame.sourceSpotCount;

        std::array<
            LocalLightCandidate,
            kLocalLightFrameCapacity> selected{};
        uint32_t selectedCount = 0u;
        uint32_t uploadablePointCount = 0u;
        uint32_t uploadableSpotCount = 0u;

        for (const PointLight& pointLight : environment.pointLights) {
            if (!IsLocalLightUploadable(pointLight)) {
                continue;
            }
            ++uploadablePointCount;
            InsertLocalLightCandidate(
                selected,
                selectedCount,
                BuildPointCandidate(pointLight));
        }
        for (const SpotLight& spotLight : environment.spotLights) {
            if (!IsLocalLightUploadable(spotLight)) {
                continue;
            }
            ++uploadableSpotCount;
            InsertLocalLightCandidate(
                selected,
                selectedCount,
                BuildSpotCandidate(spotLight));
        }

        frame.uploadableCount =
            uploadablePointCount + uploadableSpotCount;
        frame.uploadedCount = selectedCount;
        for (uint32_t index = 0u;
             index < selectedCount;
             ++index) {
            const LocalLightCandidate& candidate = selected[index];
            frame.lights[index] = candidate.gpu;
            if (candidate.type == LocalLightType::Point) {
                ++frame.uploadedPointCount;
            } else {
                ++frame.uploadedSpotCount;
            }
        }
        frame.clampedPointCount =
            uploadablePointCount - frame.uploadedPointCount;
        frame.clampedSpotCount =
            uploadableSpotCount - frame.uploadedSpotCount;
        frame.clampedCount =
            frame.clampedPointCount + frame.clampedSpotCount;
        return frame;
    }

} // namespace HIKARI::RENDER3D::LIGHTING
