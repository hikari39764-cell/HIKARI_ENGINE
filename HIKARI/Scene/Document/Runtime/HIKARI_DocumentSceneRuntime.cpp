#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <filesystem>
#include <memory>
#include <sstream>
#include <utility>

#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingCatalogJson.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackend.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

namespace HIKARI {

    bool DocumentSceneBase::IsRuntimePlayActive() const {
        return state_->runtime.playActive;
    }

    bool DocumentSceneBase::ApplySystemRuntimeChanges() {
        state_->runtime.systemScheduler.DetachWorld(state_->runtime.world);
        state_->runtime.systemScheduler.Clear();
        state_->runtime.fixedStepClock.Reset();
        RenderSubmissionSystem::InvalidateSceneResources(true);

        const bool configured = BuildSystemScheduleFromDocument();
        state_->runtime.systemScheduler.AttachWorld(state_->runtime.world);

        if (state_->runtime.playActive) {
            state_->camera.runtimeSceneCameraActive = HasRuntimeSceneCameraDriver();
            state_->camera.runtimePreviewCameraActive = !state_->camera.runtimeSceneCameraActive;
        }
        return configured;
    }
    bool DocumentSceneBase::BeginRuntimePlay() {
        if (state_->runtime.playActive || !state_->identity.currentSceneAssetGuid.IsValid()) {
            return false;
        }

        EndEditorCameraPreview();
        const bool preserveEditorState =
            SERVICES::IsEditorHost();
        if (preserveEditorState) {
            state_->camera.editorCameraSnapshot =
                state_->camera.editorCamera;
            state_->camera.editorDebugCameraSnapshot =
                state_->camera.debugCamera;
            state_->editor.componentGizmoSnapshot =
                state_->editor.componentGizmoState;
            state_->editor.overlaySnapshot =
                state_->editor.viewportOverlayState;
            state_->editor.performanceSnapshot =
                state_->editor.viewportPerformanceState;
            state_->editor.debugViewSnapshot =
                state_->editor.viewportDebugViewState;
            state_->editor.selectedGizmoObjectSnapshot =
                state_->editor.selectedGizmoObjectId;
            state_->editor.documentSnapshot = state_->identity.document;
            state_->editor.environmentSnapshot =
                state_->lighting.environment;
            state_->runtime.editorDocumentDirtySnapshot =
                state_->identity.documentDirty;
            state_->runtime.editorDocumentSnapshotValid = true;

            // In-process Play must test the document currently visible in
            // the editor, including unsaved component changes. Rebuilding
            // from disk here silently discarded the latest authoring state.
            if (!RebuildRuntimeWorld()) {
                state_->identity.document =
                    std::move(
                        state_->editor.documentSnapshot);
                state_->lighting.environment =
                    state_->editor.environmentSnapshot;
                state_->identity.documentDirty =
                    state_->runtime.editorDocumentDirtySnapshot;
                state_->runtime.editorDocumentSnapshotValid = false;
                (void)RebuildRuntimeWorld();
                state_->camera.editorCamera =
                    state_->camera.editorCameraSnapshot;
                state_->camera.debugCamera =
                    state_->camera.editorDebugCameraSnapshot;
                HIKARI_LOG_ERROR(
                    "Runtime Play scene rebuild failed.");
                return false;
            }
            ++state_->identity.documentRevision;
            state_->camera.editorCamera =
                state_->camera.editorCameraSnapshot;
        }
        else {
            // Standalone hosts already own a freshly built runtime World.
            // They must not retain an editor document snapshot or rebuild
            // the same scene a second time.
            state_->runtime.editorDocumentSnapshotValid = false;
        }

        state_->camera.gameplayCamera =
            state_->camera.editorCamera;
        state_->camera.sequencePlayback.Reset();
        state_->camera.currentCameraSequenceHandle = {};
        state_->camera.rigService.Clear();
        state_->camera.director.Reset();
        state_->camera.director.SetBaseCamera(
            state_->identity.document.camera.defaultCameraObjectId.value_or(SceneObjectId{}));
        state_->camera.resolvedFrame = {};
        if (preserveEditorState) {
            state_->camera.runtimePreviewCamera = state_->camera.editorDebugCameraSnapshot;
        }
        state_->camera.runtimePreviewCamera.ResetFromCamera(
            state_->camera.gameplayCamera);
        state_->camera.runtimePreviewCamera.SetEnabled(true);
        state_->camera.runtimeSceneCameraActive = HasRuntimeSceneCameraDriver();
        state_->camera.runtimePreviewCameraActive = !state_->camera.runtimeSceneCameraActive;
        if (preserveEditorState) {
            state_->editor.componentGizmoState = {};
            state_->editor.viewportOverlayState = {};
            state_->editor.viewportDebugViewState = {};
            state_->editor.selectedGizmoObjectId = {};
        }
        INPUT::InputContextStack& inputContexts =
            SERVICES::GetInputService().Contexts();
        if (preserveEditorState) {
            state_->runtime.editorInputContextWasActive =
                inputContexts.IsActive("Editor");
            state_->runtime.gameplayInputContextWasActive =
                inputContexts.IsActive("Gameplay");
            state_->runtime.inputContextSnapshotValid = true;
        }
        else {
            state_->runtime.inputContextSnapshotValid = false;
        }
        inputContexts.SetActive("Editor", false);
        inputContexts.SetActive("Gameplay", true);
        state_->runtime.playActive = true;
        state_->runtime.fixedStepClock.Reset();
        HIKARI_LOG_INFO("Document scene entered runtime Play state.");
        return true;
    }
    bool DocumentSceneBase::EndRuntimePlay() {
        if (!state_->runtime.playActive) {
            return true;
        }

        state_->runtime.playActive = false;
        state_->runtime.fixedStepClock.Reset();
        if (state_->runtime.inputContextSnapshotValid) {
            INPUT::InputContextStack& inputContexts =
                SERVICES::GetInputService().Contexts();
            inputContexts.SetActive(
                "Editor",
                state_->runtime.editorInputContextWasActive);
            inputContexts.SetActive(
                "Gameplay",
                state_->runtime.gameplayInputContextWasActive);
            state_->runtime.inputContextSnapshotValid = false;
        }
        state_->camera.runtimePreviewCameraActive = false;
        state_->camera.runtimeSceneCameraActive = false;
        state_->camera.sequencePlayback.Reset();
        state_->camera.currentCameraSequenceHandle = {};
        state_->camera.director.Reset();
        state_->camera.rigService.Clear();
        if (!state_->runtime.editorDocumentSnapshotValid) {
            // Standalone hosts do not own editor snapshots to restore.
            // Their current scene and runtime state are destroyed by the
            // host after leaving Play.
            state_->camera.resolvedFrame = {};
            HIKARI_LOG_INFO(
                "Standalone document scene left runtime Play state.");
            return true;
        }

        state_->identity.document = std::move(state_->editor.documentSnapshot);
        state_->lighting.environment =
            state_->editor.environmentSnapshot;
        state_->identity.documentDirty =
            state_->runtime.editorDocumentDirtySnapshot;
        state_->runtime.editorDocumentSnapshotValid = false;
        const bool restored = RebuildRuntimeWorld();
        ++state_->identity.documentRevision;
        state_->camera.editorCamera = state_->camera.editorCameraSnapshot;
        state_->camera.gameplayCamera = state_->camera.editorCamera;
        state_->camera.debugCamera = state_->camera.editorDebugCameraSnapshot;
        state_->editor.componentGizmoState = state_->editor.componentGizmoSnapshot;
        state_->editor.viewportOverlayState = state_->editor.overlaySnapshot;
        state_->editor.viewportPerformanceState = state_->editor.performanceSnapshot;
        state_->editor.viewportDebugViewState = state_->editor.debugViewSnapshot;
        state_->editor.selectedGizmoObjectId = state_->editor.selectedGizmoObjectSnapshot;
        state_->camera.resolvedFrame = {};
        state_->camera.resolvedFrame.camera = state_->camera.editorCamera;
        state_->camera.resolvedFrame.cameraCut = true;
        state_->camera.resolvedFrame.valid = true;
        state_->camera.editorCameraCutPending = true;
        if (!restored) {
            HIKARI_LOG_ERROR("Editor scene restoration after runtime Play failed.");
            return false;
        }
        HIKARI_LOG_INFO("Document scene restored after runtime Play.");
        return true;
    }
    bool DocumentSceneBase::RegisterRuntimeFeatures() {
        state_->runtime.componentRegistry.Clear();
        state_->runtime.systemTypeRegistry.Clear();
        state_->runtime.componentSystemPolicy.Clear();
        state_->runtime.featureCatalog = CreateBuiltInRuntimeFeatureCatalog();
        const bool extensionsRegistered =
            state_->runtime.extensionHost.RegisterRuntimeFeatures(
                state_->runtime.featureCatalog);
        if (!extensionsRegistered) {
            HIKARI_LOG_ERROR(
                "[RuntimeExtension] one or more extensions failed to register features");
        }

        ProjectSettingsService projectSettings{};
        const bool settingsLoaded = projectSettings.Load(
            state_->assets.Database().GetProjectRoot());
        if (!settingsLoaded) {
            HIKARI_LOG_WARN(
                "[RuntimeFeature] project settings could not be loaded; defaults are used");
        }

        RuntimeFeatureContext context =
            RuntimeFeatureContextAccess::Create(
            state_->runtime.componentRegistry,
            state_->runtime.systemTypeRegistry,
            state_->runtime.componentSystemPolicy);
        state_->runtime.featureInstallReport =
            state_->runtime.featureCatalog.RegisterEnabled(
                context,
                projectSettings.GetSettings().enabledRuntimeFeatures);
        state_->runtime.featureInstallReport.success =
            state_->runtime.featureInstallReport.success && extensionsRegistered;

        for (const std::string& issue :
                state_->runtime.featureInstallReport.resolution.issues) {
            HIKARI_LOG_ERROR("[RuntimeFeature] " + issue);
        }
        for (const std::string& featureId :
                state_->runtime.featureInstallReport.resolution.
                    implicitlyEnabledFeatures) {
            HIKARI_LOG_INFO(
                "[RuntimeFeature] dependency enabled: " + featureId);
        }
        for (const std::string& featureId :
                state_->runtime.featureInstallReport.installedFeatures) {
            HIKARI_LOG_INFO("[RuntimeFeature] installed: " + featureId);
        }
        for (const std::string& featureId :
                state_->runtime.featureInstallReport.failedFeatures) {
            HIKARI_LOG_ERROR("[RuntimeFeature] failed: " + featureId);
        }
        if (!state_->runtime.featureCatalog.IsFeatureActive(
                RuntimeFeatureIds::Cinematics)) {
            state_->camera.sequencePlayback.Reset();
            state_->camera.currentCameraSequenceHandle = {};
        }
        return state_->runtime.featureInstallReport.success;
    }

    bool DocumentSceneBase::ConfigureRuntimeWorldServices() {
        BINDING::Catalog bindingCatalog{};
        const std::filesystem::path bindingCatalogPath =
            BINDING::GetCatalogPath(
                state_->assets.Database().GetProjectRoot());
        std::string bindingCatalogMessage{};
        if (std::filesystem::exists(bindingCatalogPath)) {
            if (!BINDING::LoadCatalog(
                    bindingCatalogPath,
                    bindingCatalog,
                    bindingCatalogMessage)) {
                HIKARI_LOG_ERROR(
                    "[RuntimeBinding] " + bindingCatalogMessage);
                return false;
            }
        }
        if (!state_->runtime.bindingStore.Initialize(
                std::move(bindingCatalog))) {
            HIKARI_LOG_ERROR(
                "[RuntimeBinding] catalog initialization failed");
            return false;
        }
        state_->runtime.vfxPlaybackService.SetProgramResolver(
            [registry = &state_->assets.Registry()](
                const AssetGuid& guid,
                std::string& outError)
                -> std::shared_ptr<const VFX::VfxProgram> {
                const AssetDescriptor* base =
                    registry->FindDescriptor(guid.value);
                const auto* descriptor =
                    dynamic_cast<const VfxAssetDescriptor*>(base);
                if (descriptor == nullptr || !descriptor->program) {
                    outError =
                        "compiled VFX asset is not registered: " +
                        guid.value;
                    return {};
                }
                outError.clear();
                return descriptor->program;
            });
        state_->runtime.vfxPlaybackService.
            SetReliableRenderDeliveryEnabled(true);

        std::string physicsSettingsMessage{};
        if (!state_->runtime.physicsSettings.Load(
                state_->assets.Database().GetProjectRoot() /
                    "ProjectSettings/Physics/collision.json",
                physicsSettingsMessage)) {
            HIKARI_LOG_WARN(
                "[Physics] " + physicsSettingsMessage +
                "; using built-in defaults");
            state_->runtime.physicsSettings.ResetToDefaults();
        }
        state_->runtime.playStateService.active = &state_->runtime.playActive;
        state_->runtime.gameplayCameraService.camera = &state_->camera.gameplayCamera;
        state_->runtime.gameplayCameraService.runtimeSceneCameraActive =
            &state_->camera.runtimeSceneCameraActive;
        state_->runtime.collisionGeometryStore.SetAssetDatabase(&state_->assets.Database());
        if (!state_->runtime.physicsWorldService.HasBackend() &&
            !state_->runtime.physicsWorldService.InstallBackend(
                PHYSICS::CreateJoltPhysicsBackend())) {
            HIKARI_LOG_ERROR(
                "[Physics] failed to install default Jolt backend");
            return false;
        }

        WorldServiceRegistry& services = state_->runtime.world.Services();
        services.Clear();
        bool success = services.Register(SERVICES::GetInputService());
        success = services.Register(state_->runtime.bindingStore) && success;
        success = services.Register(
            state_->runtime.vfxPlaybackService) && success;
        success = services.Register(state_->camera.sequencePlayback) && success;
        success = services.Register(state_->runtime.playStateService) && success;
        success = services.Register(state_->runtime.gameplayCameraService) && success;
        success = services.Register(state_->camera.director) && success;
        success = services.Register(state_->camera.rigService) && success;
        success = services.Register(state_->runtime.animationPoseService) && success;
        success = services.Register(state_->runtime.animationStateMachineAssets) &&
            success;
        success = services.Register(state_->runtime.animationStateMachineRuntimeService) &&
            success;
        success = services.Register(state_->runtime.motionIntentService) && success;
        success = services.Register(state_->runtime.characterMotionStateService) &&
            success;
        success = services.Register(state_->runtime.kinematicMotionService) && success;
        success = services.Register(state_->runtime.presentationTransformService) &&
            success;
        success = services.Register(state_->runtime.collisionGeometryStore) &&
            success;
        success = services.Register(state_->runtime.physicsStatusService) &&
            success;
        success = services.Register(state_->runtime.physicsSettings) && success;
        success = services.Register(state_->runtime.physicsWorldService) && success;
        success = state_->runtime.extensionHost.RegisterWorldServices(services) &&
            success;
        return success;
    }
    bool DocumentSceneBase::BuildSystemScheduleFromDocument() {
        if (state_->identity.document.systems.empty()) {
            state_->identity.document.systems =
                state_->runtime.featureCatalog.CreateDefaultSceneSystems();
        }

        bool fullyConfigured = true;
        for (const SceneSystemData& entry : state_->identity.document.systems) {
            if (!entry.enabled) {
                continue;
            }

            if (!state_->runtime.systemTypeRegistry.Find(entry.systemId)) {
                const std::string owner =
                    state_->runtime.featureCatalog.FindOwningFeatureForSystem(
                        entry.systemId);
                if (!owner.empty() &&
                    !state_->runtime.featureCatalog.IsFeatureActive(owner)) {
                    HIKARI_LOG_WARN(
                        "[RuntimeFeature] disabled system preserved: " +
                        entry.systemId + " feature=" + owner);
                    continue;
                }
                HIKARI_LOG_WARN(
                    "[SceneSystem] unavailable system skipped: " + entry.systemId);
                fullyConfigured = false;
                continue;
            }

            std::unique_ptr<ISystem> system =
                state_->runtime.systemTypeRegistry.Create(entry.systemId, entry.settings);
            if (!system) {
                HIKARI_LOG_ERROR(
                    "[SceneSystem] factory failed: " + entry.systemId);
                fullyConfigured = false;
                continue;
            }

            if (!state_->runtime.systemScheduler.AddSystem(
                    entry.systemId,
                    entry.executionOrder,
                    std::move(system))) {
                HIKARI_LOG_WARN(
                    "[SceneSystem] duplicate or invalid system skipped: " + entry.systemId);
                fullyConfigured = false;
            }
        }

        const ComponentSystemInstallResult componentSystems =
            state_->runtime.componentSystemPolicy.InstallRequiredSystems(
                state_->identity.document,
                state_->runtime.systemTypeRegistry,
                state_->runtime.systemScheduler);
        if (!componentSystems.success) {
            fullyConfigured = false;
            for (const std::string& systemId :
                    componentSystems.unavailableSystems) {
                HIKARI_LOG_ERROR(
                    "[SceneSystem] component companion unavailable: " +
                    systemId);
            }
        }

        std::ostringstream schedule;
        const std::vector<std::string> order = state_->runtime.systemScheduler.GetExecutionOrder();
        for (size_t i = 0; i < order.size(); ++i) {
            if (i > 0) {
                schedule << ", ";
            }
            schedule << order[i];
        }
        HIKARI_LOG_INFO(
            "[SceneSystem] active schedule: " +
            (order.empty() ? std::string("<none>") : schedule.str()));
        return fullyConfigured;
    }

    bool DocumentSceneBase::ReloadSceneDocument() {
        if (state_->identity.currentSceneAssetGuid.IsValid()) {
            return OpenSceneAssetNow(state_->identity.currentSceneAssetGuid);
        }
        return OpenStartupSceneAsset();
    }
    bool DocumentSceneBase::RebuildRuntimeWorld() {
        LIGHTING_AUTHORING::SynchronizeLegacyLightingViews(
            state_->identity.document);
        state_->runtime.systemScheduler.DetachWorld(state_->runtime.world);
        state_->runtime.systemScheduler.Clear();
        state_->runtime.fixedStepClock.Reset();
        RenderSubmissionSystem::InvalidateSceneResources(true);

        SceneDependencySet deps = state_->runtime.builder.CollectDependencies(
            state_->identity.document,
            &state_->runtime.componentRegistry);
        if (state_->assets.NeedsRuntimeDependencyRegistryRefresh(deps)) {
            // Editor 蛛ｴ縺ｧ霑ｽ蜉繝ｻ蜀絞mport 縺輔ｌ縺・asset descriptor 繧・runtime build 蜑阪↓蜷梧悄縺吶ｋ縲・
            if (ReloadAssets()) {
                deps = state_->runtime.builder.CollectDependencies(
                    state_->identity.document,
                    &state_->runtime.componentRegistry);
            } else {
                HIKARI_LOG_WARN("[SceneRuntime] asset registry refresh failed before runtime rebuild.");
            }
        }

        state_->runtime.builder.PreloadDependencies(
            deps,
            state_->assets.Registry(),
            state_->assets.Models(),
            state_->lighting.sky,
            state_->assets.Database().GetProjectRoot(),
            state_->identity.currentSceneAssetGuid.value);
        for (const RuntimeFeatureSceneIssue& issue :
                state_->runtime.featureCatalog.AnalyzeSceneDocument(state_->identity.document)) {
            HIKARI_LOG_WARN(
                "[RuntimeFeature] scene data preserved but inactive: " +
                issue.itemId + " feature=" + issue.featureId +
                (issue.objectId.value != 0
                    ? " objectId=" + std::to_string(issue.objectId.value)
                    : std::string{}));
        }
        const bool built = state_->runtime.builder.BuildWorldFromDocument(state_->identity.document, state_->runtime.world, state_->assets.Registry(), state_->runtime.componentRegistry, state_->assets.Models(), state_->lighting.sky);

        state_->lighting.environment = state_->identity.document.environment;
        state_->lighting.environment.directional.direction = MATH::Normalize(state_->lighting.environment.directional.direction);

        RuntimeSceneContext::SetCurrentWorld(&state_->runtime.world);
        RuntimeSceneContext::ResolvePendingSceneEntry(state_->runtime.world, state_->identity.sceneId);

        if (built) {
            BuildSystemScheduleFromDocument();
            state_->runtime.systemScheduler.AttachWorld(state_->runtime.world);
        }
        ApplyCameraRuntimeChanges();
        return built;
    }

} // namespace HIKARI
