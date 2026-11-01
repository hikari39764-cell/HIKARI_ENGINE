#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"

#include "Tools/Baking/HIKARI_LightingBakeReport.h"
#include "Tools/Baking/HIKARI_LightProbeBaker.h"
#include "Tools/Baking/HIKARI_ProbeCubemapCaptureTarget.h"
#include "Tools/Baking/HIKARI_ReflectionProbeBaker.h"
#include "Tools/Baking/Backends/HIKARI_DxrProgressiveLightmapBaker.h"
#include "Render3D/Lighting/Transport/HIKARI_LightingTransportScene.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

class Camera3D;
class DocumentSceneBase;
struct SceneEnvironment;

class DocumentSceneBakeCoordinator final {
public:
    DocumentSceneBakeCoordinator() = default;
    ~DocumentSceneBakeCoordinator() = default;

    DocumentSceneBakeCoordinator(const DocumentSceneBakeCoordinator&) = delete;
    DocumentSceneBakeCoordinator& operator=(const DocumentSceneBakeCoordinator&) = delete;

    bool RequestReflectionProbeBake(DocumentSceneBase& scene, SceneObjectId targetObjectId = {});
    bool RequestLightProbeBake(DocumentSceneBase& scene, SceneObjectId targetObjectId = {});
    bool RequestLightmapBake(DocumentSceneBase& scene, SceneObjectId targetObjectId = {});
    bool RequestLightingBake(DocumentSceneBase& scene, bool force, SceneObjectId selectedObjectId);
    bool ClearLightingBake(DocumentSceneBase& scene);
    TOOLS::BAKING::LightingBakeJobState GetJobState() const;
    bool HasLastReport() const;
    const TOOLS::BAKING::LightingBakeReport& GetLastReport() const;
    bool ProcessReflectionProbeBakeJob(DocumentSceneBase& scene);
    bool ProcessLightProbeBakeJob(DocumentSceneBase& scene);
    bool ProcessLightmapBakeJob(DocumentSceneBase& scene);

private:
    struct ReflectionProbeJob {
        TOOLS::BAKING::LightingBakeJobState state = TOOLS::BAKING::LightingBakeJobState::Idle;
        TOOLS::BAKING::ReflectionProbeBakeRequest request{};
        TOOLS::BAKING::LightingBakeReport report{};
        TOOLS::BAKING::ProbeCubemapCaptureTarget captureTarget{};
        uint64_t fenceValue = 0;
        uint32_t nextFaceIndex = 0;
    };

    struct LightProbeJob {
        TOOLS::BAKING::LightingBakeJobState state = TOOLS::BAKING::LightingBakeJobState::Idle;
        TOOLS::BAKING::LightProbeBakeRequest request{};
        TOOLS::BAKING::LightingBakeReport report{};
        TOOLS::BAKING::ProbeCubemapCaptureTarget captureTarget{};
        uint64_t fenceValue = 0;
        uint32_t nextProbeIndex = 0;
        uint32_t nextFaceIndex = 0;
        std::vector<std::filesystem::path> probeCapturePaths{};
    };

    struct LightmapJob {
        ~LightmapJob();
        TOOLS::BAKING::LightingBakeJobState state = TOOLS::BAKING::LightingBakeJobState::Idle;
        TOOLS::BAKING::LightingBakeReport report{};
        RENDER3D::LIGHTING::TRANSPORT::LightingTransportScene transportScene{};
        TOOLS::BAKING::DxrProgressiveLightmapBaker baker{};
        RENDER3D::LIGHTING::TRANSPORT::LightingTransportBakeSettings settings{};
        SceneObjectId objectId{};
        size_t targetIndex = 0u;
        size_t pageIndex = 0u;
        uint64_t resourceReleaseFence = 0u;
        bool scenePrepared = false;
        bool outputsCommitted = false;
        std::filesystem::path outputDirectory{};
        std::vector<ASSETS::LIGHTING::LightmapBakePage> completedPages{};
        std::filesystem::path projectRoot{};
        std::filesystem::path outputPath{};
        std::string sceneGuid{};
        std::string sceneHash{};
        std::string authoringHash{};
        std::string dependencyHash{};
        std::string recordId{};
        std::string objectName{};
        std::string bakeSet{"Default"};
        std::string scenario{"Default"};
    };

    bool StartNextQueuedBake(DocumentSceneBase& scene);

    std::unique_ptr<ReflectionProbeJob> reflectionProbeJob_{};
    std::unique_ptr<LightProbeJob> lightProbeJob_{};
    std::unique_ptr<LightmapJob> lightmapJob_{};
    enum class QueuedBakeKind : uint8_t {
        ReflectionProbe,
        IrradianceVolume,
        LightmapObject,
    };
    struct QueuedBakeItem {
        QueuedBakeKind kind = QueuedBakeKind::ReflectionProbe;
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
