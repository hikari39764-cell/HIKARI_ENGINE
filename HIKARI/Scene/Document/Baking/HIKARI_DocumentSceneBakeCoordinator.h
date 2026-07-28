#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "Tools/Baking/HIKARI_LightingBakeReport.h"
#include "Tools/Baking/HIKARI_LightProbeBaker.h"
#include "Tools/Baking/HIKARI_ProbeCubemapCaptureTarget.h"
#include "Tools/Baking/HIKARI_ReflectionProbeBaker.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class Camera3D;
    class DocumentSceneBase;
    struct SceneEnvironment;

    class DocumentSceneBakeCoordinator final {
    public:
        DocumentSceneBakeCoordinator() = default;
        ~DocumentSceneBakeCoordinator() = default;

        DocumentSceneBakeCoordinator(
            const DocumentSceneBakeCoordinator&) = delete;
        DocumentSceneBakeCoordinator& operator=(
            const DocumentSceneBakeCoordinator&) = delete;

        bool RequestReflectionProbeBake(
            DocumentSceneBase& scene,
            SceneObjectId targetObjectId = {});
        bool RequestLightProbeBake(
            DocumentSceneBase& scene,
            SceneObjectId targetObjectId = {});
        bool RequestLightingBake(
            DocumentSceneBase& scene,
            bool force,
            SceneObjectId selectedObjectId);
        TOOLS::BAKING::LightingBakeJobState GetJobState() const;
        bool HasLastReport() const;
        const TOOLS::BAKING::LightingBakeReport& GetLastReport() const;
        bool ProcessReflectionProbeBakeJob(DocumentSceneBase& scene);
        bool ProcessLightProbeBakeJob(DocumentSceneBase& scene);

    private:
        enum class ProbeCaptureKind {
            ReflectionProbe,
            LightProbe,
        };

        struct ReflectionProbeJob {
            TOOLS::BAKING::LightingBakeJobState state =
                TOOLS::BAKING::LightingBakeJobState::Idle;
            TOOLS::BAKING::ReflectionProbeBakeRequest request{};
            TOOLS::BAKING::LightingBakeReport report{};
            TOOLS::BAKING::ProbeCubemapCaptureTarget captureTarget{};
            uint64_t fenceValue = 0;
            uint32_t nextFaceIndex = 0;
        };

        struct LightProbeJob {
            TOOLS::BAKING::LightingBakeJobState state =
                TOOLS::BAKING::LightingBakeJobState::Idle;
            TOOLS::BAKING::LightProbeBakeRequest request{};
            TOOLS::BAKING::LightingBakeReport report{};
            TOOLS::BAKING::ProbeCubemapCaptureTarget captureTarget{};
            uint64_t fenceValue = 0;
            uint32_t nextProbeIndex = 0;
            uint32_t nextFaceIndex = 0;
            std::vector<std::filesystem::path> probeCapturePaths{};
        };

        bool RenderProbeCaptureFace(
            DocumentSceneBase& scene,
            TOOLS::BAKING::ProbeCubemapCaptureTarget& captureTarget,
            const Camera3D& faceCamera,
            const SceneEnvironment& captureEnvironment,
            uint32_t faceIndex,
            ProbeCaptureKind kind);
        bool StartNextQueuedBake(DocumentSceneBase& scene);

        std::unique_ptr<ReflectionProbeJob> reflectionProbeJob_{};
        std::unique_ptr<LightProbeJob> lightProbeJob_{};
        struct QueuedBakeItem {
            bool reflectionProbe = true;
            SceneObjectId objectId{};
        };
        std::vector<QueuedBakeItem> queuedBakeItems_{};
        uint32_t batchItemCount_ = 0;
        uint32_t batchCompletedItemCount_ = 0;
        uint32_t batchReusedItemCount_ = 0;
        bool batchForce_ = false;
        TOOLS::BAKING::LightingBakeReport lastReport_{};
        bool hasLastReport_ = false;
    };

} // namespace HIKARI
