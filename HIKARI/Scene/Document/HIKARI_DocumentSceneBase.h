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
#include "Scene/Environment/HIKARI_EnvironmentRuntime.h"
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
#if defined(HIKARI_WITH_EDITOR)
#include "Tools/Baking/HIKARI_LightingBakeReport.h"
#endif

namespace HIKARI {

class DocumentSceneAssetBindings;
struct DocumentSceneState;
namespace RENDERSTYLE {
class RenderStyleAssetStore;
}
#if defined(HIKARI_WITH_EDITOR)
class DocumentSceneBakeCoordinator;
namespace DOCUMENT_SCENE_BAKING {
class RasterLightingTransportBackend;
}
#endif

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

    const std::string& GetSceneId() const override;
    const std::string& GetScenePath() const;
    const std::string& GetSceneLoadError() const noexcept;
    uint64_t GetSceneDocumentRevision() const noexcept;

    World& GetWorld();
    const World& GetWorld() const;

    SceneDocument& GetSceneDocument();
    const SceneDocument& GetSceneDocument() const;

    // このフレームの描画入力（環境プロファイル + 時刻・天気）。シーン文書の環境は参照だけを持つ。
    SceneEnvironment& GetSceneEnvironment();
    const SceneEnvironment& GetSceneEnvironment() const;
    // 主ビューが今使っている露出の EV100（補正込み）。自動露出は直近の測光の結果、手動は設定から。
    float GetCurrentExposureEv100() const;
    ENVIRONMENT::EnvironmentRuntime& GetEnvironmentRuntime() noexcept;
    // 編集中の時刻の流れ（実時間 1 秒あたりの秒数）。天気の遷移・風・雲は編集中も常に実時間で進む。
    void SetEnvironmentPreviewTimeScale(double timeScale) noexcept;
    ENVIRONMENT::EnvironmentAssetStore& GetEnvironmentAssets() noexcept;
    RENDERSTYLE::RenderStyleAssetStore& GetRenderStyleAssets() noexcept;
    // シーンの依存（空の資産は解決済みの環境プロファイルから）。
    SceneDependencySet CollectRuntimeDependencies() const;
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
    bool RebuildRuntimeWorld(bool reconfigureWorldServices = false, bool assetSnapshotAlreadyRefreshed = false,
                             bool* outOldWorldDetached = nullptr);
    // document のオブジェクトの追加・削除・変更を runtime world へ差分反映する（変更分は作り直す）。
    // Play 中、未構築の World、資産記述子の再読込や新しいシステムの導入が必要な場合は
    // RebuildRuntimeWorld で全体を作り直す。changes は反映済みの document に対する差分であること。
    bool ApplyRuntimeObjectChanges(const SceneObjectChanges& changes);
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
    // 環境の実行時状態を文書の参照と初期状態から作り直す。
    void ResetEnvironmentRuntime();
    // 環境プロファイル（編集中の下書きを含む）を引き直す。時刻と天気の進行は保ち、空の資源が変わった時だけ読み直す。
    bool RefreshEnvironmentProfile();
    bool RefreshTextureRuntimeByPath(const std::string& path);
    bool ReloadModelAssetRuntime(const AssetId& modelId);
    int RebindModelComponents();
    int RebuildMaterialOverrides();
    int RebuildMaterialOverridesForMaterial(const AssetGuid& materialGuid);
    int ApplyRuntimeMaterialOverridePreview(const AssetGuid& materialGuid, const MaterialAssetData& data);
    int ApplyRuntimeMaterialOverridePreview(const AssetGuid& materialGuid, const PbrMaterialAssetData& data);
#if defined(HIKARI_WITH_EDITOR)
    // 照明の焼き込み（編集器専用）。
    bool RequestLightingBake(bool force, SceneObjectId selectedObjectId = {});
    bool ClearLightingBake();
    TOOLS::BAKING::LightingBakeJobState GetLightingBakeJobState() const;
    bool HasLastLightingBakeReport() const;
    const TOOLS::BAKING::LightingBakeReport& GetLastLightingBakeReport() const;
#endif
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
#if defined(HIKARI_WITH_EDITOR)
    friend class DocumentSceneBakeCoordinator;
    friend class DOCUMENT_SCENE_BAKING::RasterLightingTransportBackend;
#endif
    bool OpenSceneAssetWithCurrentAssets(const AssetGuid& sceneGuid);
    bool OpenStartupSceneAssetWithCurrentAssets();
    std::unique_ptr<DocumentSceneState> state_;
};

} // namespace HIKARI
