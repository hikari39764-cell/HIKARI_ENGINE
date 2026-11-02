#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <utility>

#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

namespace HIKARI {

bool DocumentSceneBakeCoordinator::RequestLightingBake(DocumentSceneBase& scene, bool force,
                                                       SceneObjectId selectedObjectId) {
    if (!TOOLS::BAKING::LightingBakeService{}.GetBlockReason(scene).empty())
        return false;

    if (scene.state_->assets.Database().GetProjectRoot().empty()) {
        scene.state_->assets.Database().Initialize({});
    }

    TOOLS::BAKING::LightingBakeRequest request = TOOLS::BAKING::LightingBakeService::BuildRequest(scene);
    request.force = force;

    TOOLS::BAKING::LightingBakeService service{};
    const TOOLS::BAKING::LightingBakeStatus status = service.AnalyzeLightingBakeStatus(request);
    TOOLS::BAKING::LightingBakeReport report{};
    report.action = TOOLS::BAKING::LightingBakeAction::BakeLighting;
    report.target = TOOLS::BAKING::LightingBakeTarget::All;
    report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(request.projectRoot, request.sceneGuid);
    report.manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(request.projectRoot, request.sceneGuid);
    if (!status.valid) {
        report.success = false;
        report.errors = status.warnings;
        report.jobState = TOOLS::BAKING::LightingBakeJobState::Failed;
        lastReport_ = std::move(report);
        hasLastReport_ = true;
        return false;
    }

    queuedBakeItems_.clear();
    batchWarnings_.clear();
    batchItemCount_ = 0u;
    batchCompletedItemCount_ = 0u;
    batchReusedItemCount_ = 0u;
    batchForce_ = force;
    for (const TOOLS::BAKING::LightingBakeItemStatus& item : status.items) {
        if (!item.enabled || (selectedObjectId.value != 0u && item.objectId != selectedObjectId.value)) {
            continue;
        }
        ++batchItemCount_;
        if (!force && !item.dirty) {
            ++batchReusedItemCount_;
            continue;
        }

        QueuedBakeKind kind = QueuedBakeKind::ReflectionProbe;
        if (item.kind == TOOLS::BAKING::LightingBakeItemKind::IrradianceVolume) {
            kind = QueuedBakeKind::IrradianceVolume;
        } else if (item.kind == TOOLS::BAKING::LightingBakeItemKind::LightmapObject) {
            kind = QueuedBakeKind::LightmapObject;
        }
        queuedBakeItems_.push_back(QueuedBakeItem{kind, SceneObjectId{item.objectId}});
        if (item.dirty) {
            report.dirtyReasons.push_back(item.name + ": " + TOOLS::BAKING::ToString(item.state));
        }
    }

    report.bakeItemCount = batchItemCount_;
    report.dirtyItemCount = static_cast<uint32_t>(queuedBakeItems_.size());
    report.reusedItemCount = batchReusedItemCount_;
    if (batchItemCount_ == 0u) {
        report.success = false;
        report.jobState = TOOLS::BAKING::LightingBakeJobState::Failed;
        report.errors.push_back(selectedObjectId.value != 0u
                                    ? "The selected object is not an enabled lighting bake object."
                                    : "The scene contains no enabled lighting bake objects.");
        lastReport_ = std::move(report);
        hasLastReport_ = true;
        return false;
    }
    if (queuedBakeItems_.empty()) {
        report.jobState = TOOLS::BAKING::LightingBakeJobState::Completed;
        report.completedItemCount = batchReusedItemCount_;
        report.messages.push_back("All lighting bake outputs are already up to date.");
        lastReport_ = std::move(report);
        hasLastReport_ = true;
        batchItemCount_ = 0u;
        batchReusedItemCount_ = 0u;
        batchForce_ = false;
        return true;
    }

    lastReport_ = report;
    hasLastReport_ = true;
    return StartNextQueuedBake(scene);
}

bool DocumentSceneBakeCoordinator::ClearLightingBake(DocumentSceneBase& scene) {
    TOOLS::BAKING::LightingBakeService service;
    if (!service.GetBlockReason(scene).empty())
        return false;
    TOOLS::BAKING::LightingBakeRequest request;
    request.projectRoot = scene.GetAssetDatabase().GetProjectRoot();
    request.sceneGuid = scene.GetCurrentSceneAssetGuid().value;
    lastReport_ = service.ClearLightingBake(request);
    hasLastReport_ = true;
    if (lastReport_.success)
        scene.RefreshLightingRuntime();
    return lastReport_.success;
}

bool DocumentSceneBakeCoordinator::StartNextQueuedBake(DocumentSceneBase& scene) {
    if (queuedBakeItems_.empty()) {
        lastReport_.jobState = TOOLS::BAKING::LightingBakeJobState::Completed;
        lastReport_.completedItemCount = batchCompletedItemCount_ + batchReusedItemCount_;
        lastReport_.warnings.insert(lastReport_.warnings.end(), batchWarnings_.begin(), batchWarnings_.end());
        batchWarnings_.clear();
        lastReport_.messages.push_back("Lighting bake batch completed.");
        hasLastReport_ = true;
        batchItemCount_ = 0u;
        batchCompletedItemCount_ = 0u;
        batchReusedItemCount_ = 0u;
        batchForce_ = false;
        return true;
    }

    const QueuedBakeItem next = queuedBakeItems_.front();
    queuedBakeItems_.erase(queuedBakeItems_.begin());
    bool started = false;
    switch (next.kind) {
    case QueuedBakeKind::ReflectionProbe: started = RequestReflectionProbeBake(scene, next.objectId); break;
    case QueuedBakeKind::IrradianceVolume: started = RequestLightProbeBake(scene, next.objectId); break;
    case QueuedBakeKind::LightmapObject: started = RequestLightmapBake(scene, next.objectId); break;
    }
    if (!started) {
        queuedBakeItems_.clear();
    }
    return started;
}

} // namespace HIKARI
