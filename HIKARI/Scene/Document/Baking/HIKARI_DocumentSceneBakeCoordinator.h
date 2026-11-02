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
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
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

    // 探針は光照贴图と同じ DXR の経路で、探針ごとに 16x16 の八面体の方向へ積む。
    struct LightProbeJob {
        TOOLS::BAKING::LightingBakeJobState state = TOOLS::BAKING::LightingBakeJobState::Idle;
        TOOLS::BAKING::LightProbeBakeRequest request{};
        TOOLS::BAKING::LightingBakeReport report{};
        RENDER3D::LIGHTING::TRANSPORT::LightingTransportScene transportScene{};
        TOOLS::BAKING::DxrProgressiveLightmapBaker baker{};
        std::vector<MATH::Vec3> positions{};
        std::vector<TOOLS::BAKING::DxrTransportAccumulation> cells{};
        float visibilityDistance = 0.0f;
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
        std::filesystem::path projectRoot{};
        std::filesystem::path outputPath{};
        std::string sceneGuid{};
        // 焼き上がった page を足して清单に書く記録（id、名前、hash、範囲は要求時に決まる）。
        ASSETS::LIGHTING::LightmapBakeRecord record{};
    };

    // 一括の焼き込みの一項目ずつ（StartNextQueuedBake だけが呼ぶ）。
    bool RequestReflectionProbeBake(DocumentSceneBase& scene, SceneObjectId targetObjectId);
    bool RequestLightProbeBake(DocumentSceneBase& scene, SceneObjectId targetObjectId);
    bool RequestLightmapBake(DocumentSceneBase& scene, SceneObjectId targetObjectId);
    bool StartNextQueuedBake(DocumentSceneBase& scene);
    // 光照贴图の記録を清单に書き、置き換えた古い page を退役させて実行時の光照を読み直す。
    bool CommitLightmapRecord(DocumentSceneBase& scene, const std::filesystem::path& projectRoot,
                              const std::string& sceneGuid, const ASSETS::LIGHTING::LightmapBakeRecord& record,
                              TOOLS::BAKING::LightingBakeReport& report, std::string& outMessage);
    // 光照贴图と探針の DXR の焼き込みが使う場面（参加する静的な模型の三角形、材質、光源、空）。
    RENDER3D::LIGHTING::TRANSPORT::LightingTransportScene BuildTransportScene(
        DocumentSceneBase& scene, const std::filesystem::path& projectRoot,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting);

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
    // 一括の焼き込みの途中で光照贴图を作れなかった物体。各項目の報告は上書きされるので、最後の報告にまとめて残す。
    std::vector<std::string> batchWarnings_{};
    TOOLS::BAKING::LightingBakeReport lastReport_{};
    bool hasLastReport_ = false;
};

} // namespace HIKARI
