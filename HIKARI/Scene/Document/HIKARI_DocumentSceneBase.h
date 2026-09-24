#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Models/HIKARI_ModelManager.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"
#include "Scene/Debug/HIKARI_ViewportDebugState.h"
#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackTypes.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_IScene.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Features/HIKARI_RuntimeExtension.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"
#include "Tools/Baking/HIKARI_LightingBakeReport.h"

namespace HIKARI {

class DocumentSceneAssetBindings;
class DocumentSceneBakeCoordinator;
struct DocumentSceneState;
namespace DOCUMENT_SCENE_BAKING {
class RasterLightingTransportBackend;
}

struct MaterialAssetData;
struct PbrMaterialAssetData;

class DocumentSceneBase : public IScene {
public:
    explicit DocumentSceneBase(std::string sceneId);
    ~DocumentSceneBase() override;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render() override;
    void RenderImGui() override;

    const std::string& GetSceneId() const override;
    const std::string& GetScenePath() const;
    const std::string& GetSceneLoadError() const noexcept;
    uint64_t GetSceneDocumentRevision() const noexcept;

    World& GetWorld();
    const World& GetWorld() const;

    SceneDocument& GetSceneDocument();
    const SceneDocument& GetSceneDocument() const;

    SceneEnvironment& GetSceneEnvironment();
    const SceneEnvironment& GetSceneEnvironment() const;
    AssetDatabase& GetAssetDatabase();
    const AssetDatabase& GetAssetDatabase() const;
    AssetRegistry& GetAssetRegistry();
    const AssetRegistry& GetAssetRegistry() const;
    BINDING::RuntimeBindingStore& GetRuntimeBindingStore() noexcept;
    const BINDING::RuntimeBindingStore& GetRuntimeBindingStore() const noexcept;
    bool ReloadRuntimeBindingCatalog(std::string& outMessage);
    ModelManager& GetModelManager();
    SkyManager& GetSkyManager();
    ComponentRegistry& GetComponentRegistry();
    const ComponentSystemPolicy& GetComponentSystemPolicy() const noexcept;
    const SystemTypeRegistry& GetSystemTypeRegistry() const noexcept;
    const SystemScheduler& GetSystemScheduler() const noexcept;
    const RuntimeFeatureCatalog& GetRuntimeFeatureCatalog() const noexcept;
    bool AddBorrowedRuntimeExtension(IRuntimeExtension& extension);
    bool IsRuntimeFeatureActive(std::string_view featureId) const noexcept;
    std::vector<SceneSystemData> CreateProjectDefaultSceneSystems() const;
    bool ReloadRuntimeFeaturesFromProjectSettings();
    SceneRuntimeBuilder& GetRuntimeBuilder();
    const SceneRuntimeBuilder& GetRuntimeBuilder() const;

    Camera3D& GetCamera();
    const Camera3D& GetCamera() const;
    CameraDirector& GetCameraDirector();
    const CameraDirector& GetCameraDirector() const;
    const RENDER3D::ResolvedCameraFrame& GetResolvedCameraFrame() const;
    DebugCameraController3D& GetDebugCamera();

    bool& GetEnvironmentLightingEnabled();
    bool ReloadAssets();
    bool ReloadSceneDocument();
    bool RebuildRuntimeWorld(bool reconfigureWorldServices = false, bool assetSnapshotAlreadyRefreshed = false);
    bool ApplySystemRuntimeChanges();
    bool ApplyCameraRuntimeChanges();
    bool SetGameDefaultCamera(SceneObjectId cameraObjectId);
    void ClearGameDefaultCamera();
    bool BeginEditorCameraPreview(SceneObjectId cameraObjectId, const CameraBlendDesc& blend = {},
                                  bool forceRestart = false);
    void EndEditorCameraPreview();
    bool IsEditorCameraPreviewActive() const;
    SceneObjectId GetEditorCameraPreviewObjectId() const;
    bool TryResolveCameraObjectView(SceneObjectId cameraObjectId, float aspect, Camera3D& outCamera) const;
    bool ApplyCameraObjectPose(SceneObjectId cameraObjectId, const MATH::Vec3& position, const MATH::Quat& rotation,
                               bool markDirty = true);
    bool SnapCameraObjectToEditorView(SceneObjectId cameraObjectId);
    bool PreviewSequence(const CinematicSequence& sequence, const SEQUENCER::SequenceBindingContext& bindings,
                         const SEQUENCER::SequenceEvaluationContext& context, bool contentChanged);
    void EndSequencePreview();
    bool BeginRuntimePlay();
    bool EndRuntimePlay();
    bool IsRuntimePlayActive() const;
    bool OpenSceneAssetNow(const AssetGuid& sceneGuid);
    bool OpenStartupSceneAsset();
    bool CreateTransientEmptySceneDocument();
    bool HasUnsavedSceneChanges() const;
    void SetUnsavedSceneChanges(bool dirty);
    bool ApplyDocumentEnvironmentRuntimeChanges();
    bool RefreshLightingRuntime();
    bool RefreshTextureRuntimeByPath(const std::string& path);
    bool ReloadModelAssetRuntime(const AssetId& modelId);
    int RebindModelComponents();
    int RebuildMaterialOverrides();
    int RebuildMaterialOverridesForMaterial(const AssetGuid& materialGuid);
    int ApplyRuntimeMaterialOverridePreview(const AssetGuid& materialGuid, const MaterialAssetData& data);
    int ApplyRuntimeMaterialOverridePreview(const AssetGuid& materialGuid, const PbrMaterialAssetData& data);
    bool RequestLightingBake(bool force, SceneObjectId selectedObjectId = {});
    TOOLS::BAKING::LightingBakeJobState GetLightingBakeJobState() const;
    bool HasLastLightingBakeReport() const;
    const TOOLS::BAKING::LightingBakeReport& GetLastLightingBakeReport() const;
    bool SaveCurrentSceneDocument();
    bool SaveCurrentSceneDocumentAs(const AssetGuid& sceneGuid);
    const AssetGuid& GetCurrentSceneAssetGuid() const;
    std::string GetCurrentSceneDisplayName() const;

    ComponentGizmoRegistry& GetComponentGizmoRegistry() noexcept;
    const ComponentGizmoRegistry& GetComponentGizmoRegistry() const noexcept;
    void SetComponentGizmoState(const ComponentGizmoState& state);
    void SetViewportOverlayState(const ViewportOverlayState& state);
    void SetViewportPerformanceState(const ViewportPerformanceState& state);
    void SetViewportDebugViewState(const ViewportDebugViewState& state);
    void SetSelectedGizmoObjectId(SceneObjectId id);
    SceneObjectId GetSelectedGizmoObjectId() const;

protected:
    bool RegisterRuntimeFeatures();
    bool BuildSystemScheduleFromDocument();
    bool ConfigureRuntimeWorldServices();

    virtual bool UseDebugCamera() const;
    virtual bool DrawDebugHelpers() const;
    virtual bool UseEnvironmentLighting() const;
    bool HasRuntimeSceneCameraDriver() const;

private:
    friend class DocumentSceneAssetBindings;
    friend class DocumentSceneBakeCoordinator;
    friend class DOCUMENT_SCENE_BAKING::RasterLightingTransportBackend;
    bool OpenSceneAssetWithCurrentAssets(const AssetGuid& sceneGuid);
    bool OpenStartupSceneAssetWithCurrentAssets();
    std::unique_ptr<DocumentSceneState> state_;
};

} // namespace HIKARI
