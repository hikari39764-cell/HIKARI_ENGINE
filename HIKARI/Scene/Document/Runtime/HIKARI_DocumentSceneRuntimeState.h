#pragma once

#include <memory>

#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Animation/Runtime/HIKARI_AnimationNodeOverrideService.h"
#include "Animation/Runtime/HIKARI_AnimationRootMotionService.h"
#include "Animation/Runtime/HIKARI_AnimationSocketService.h"
#include "Animation/StateMachine/HIKARI_AnimationStateMachineRuntimeService.h"
#include "Assets/HIKARI_DocumentAssetStores.h"
#include "Assets/Prefab/HIKARI_PrefabAssetStore.h"
#include "Core/HIKARI_FixedStepClock.h"
#include "Runtime/Diagnostics/HIKARI_RuntimeDebugService.h"
#include "Gameplay/Motion/HIKARI_CharacterMotionStateService.h"
#include "Gameplay/Motion/HIKARI_MotionIntentService.h"
#include "Gameplay/Dialogue/HIKARI_DialogueSessionService.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsCollisionGeometryStore.h"
#include "Physics/HIKARI_PhysicsProjectSettings.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Render3D/Material/Program/HIKARI_MaterialParameterProjection.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_PresentationModelService.h"
#include "Scene/Environment/HIKARI_EnvironmentRuntime.h"
#include "Scene/Lighting/HIKARI_LocalFogService.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_RuntimeObjectActivityService.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/Navigation/HIKARI_NavigationWorldService.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/Capture/HIKARI_SceneCaptureService.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Features/HIKARI_RuntimeExtension.h"
#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceQueryService.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/Vfx/HIKARI_ActionPostReactionService.h"
#include "Scene/Vfx/HIKARI_FluidInteractionService.h"
#include "Scene/Vfx/HIKARI_CombatAfterimageService.h"
#include "Scene/Vfx/HIKARI_CombatPresentationService.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Water/HIKARI_WaterSurfaceQueryService.h"

namespace HIKARI {

struct DocumentSceneRuntimeState {
    RUNTIME::RuntimeDebugService debugService{};
    bool developerDiagnosticsAllowed = false;
    FixedStepClock fixedStepClock{};
    RuntimePlayStateService playStateService{};
    GameplayCameraService gameplayCameraService{};
    RuntimeAssetContextService runtimeAssetContextService{};
    SCENE_CAPTURE::SceneCaptureService sceneCaptureService{};
    ANIMATION::AnimationPoseService animationPoseService{};
    ANIMATION::AnimationNodeOverrideService animationNodeOverrideService{};
    ANIMATION::AnimationSocketService animationSocketService{};
    ANIMATION::AnimationRootMotionService animationRootMotionService{};
    ANIMATION::AnimationStateMachineRuntimeService animationStateMachineRuntimeService{};
    GAMEPLAY::MotionIntentService motionIntentService{};
    GAMEPLAY::CharacterMotionStateService characterMotionStateService{};
    PHYSICS::KinematicMotionService kinematicMotionService{};
    PHYSICS::PhysicsWorldService physicsWorldService{};
    PHYSICS::PhysicsCollisionGeometryStore collisionGeometryStore{};
    PHYSICS::PhysicsRuntimeStatusService physicsStatusService{};
    PHYSICS::PhysicsProjectSettings physicsSettings{};
    NAVIGATION::NavigationWorldService navigationWorldService{};
    BINDING::RuntimeBindingStore bindingStore{};
    std::shared_ptr<const BINDING::Snapshot> materialProjectionSourceSnapshot{};
    std::shared_ptr<const RENDER3D::MATERIAL::MaterialExternalValueSnapshot> materialExternalValueSnapshot{};
    VFX::VfxPlaybackService vfxPlaybackService{};
    VFX::CombatPresentationService combatPresentationService{};
    VFX::CombatAfterimageService combatAfterimageService{};
    VFX::ActionPostReactionService actionPostReactionService{};
    GAME_UI::GameUiPresentationService gameUiPresentationService{};
    DIALOGUE::DialogueSessionService dialogueSessionService{};
    // composition root が所有し、WaterSurfaceQueryService は借用する。
    std::unique_ptr<WATER::IWaterBathymetryProvider> waterBathymetryProvider{};
    WATER::WaterSurfaceQueryService waterSurfaceQueryService{};
    TERRAIN::TerrainSurfaceQueryService terrainSurfaceQueryService{};
    PresentationModelService presentationModelService{};
    LocalFogService localFogService{};
    FluidInteractionService fluidInteractionService{};
    // 描画が回読した汇の吸い込み量（このフレームの描画が書き、次のフレームに fluidInteractionService へ渡す）。
    std::vector<RENDER3D::FLUID::FluidSinkReport> fluidSinkReports{};
    // 描画が回読した流体域の発光（このフレームの描画が書き、次のフレームに VFX の再生側へ渡す。火の光源が読む）。
    std::vector<RENDER3D::FLUID::FluidGlowReport> fluidGlowReports{};
    // 時刻・天気・環境プロファイルの実行時の所有者。シーン文書へは書き戻さない。
    ENVIRONMENT::EnvironmentRuntime environment{};
    // 編集中の時刻の流れ（実時間 1 秒あたりの秒数、0 で止める）。環境パネルの試し再生が設定する。
    double environmentPreviewTimeScale = 0.0;
    PresentationTransformService presentationTransformService{};
    RuntimeObjectActivityService runtimeObjectActivityService{};
    RuntimeExtensionHost extensionHost{};
    RuntimeFeatureCatalog featureCatalog{};
    RuntimeFeatureInstallReport featureInstallReport{};
    ComponentSystemPolicy componentSystemPolicy{};
    SystemTypeRegistry systemTypeRegistry{};
    SystemScheduler systemScheduler{};
    ComponentRegistry componentRegistry{};
    WorldMassSchemaRegistry massSchemaRegistry{};
    SceneRuntimeBuilder builder{};
    AnimationStateMachineAssetStore animationStateMachineAssets{};
    PrefabAssetStore prefabAssets{};
    DialogueAssetStore dialogueAssets{};
    // World borrows registries/services above and must be destroyed first.
    World world{};
    bool playActive = false;
    bool stopGameProjectSessionAfterWorldDetach = false;
    bool inputContextSnapshotValid = false;
    bool editorInputContextWasActive = false;
    bool gameplayInputContextWasActive = false;
    bool editorDocumentSnapshotValid = false;
    bool editorDocumentDirtySnapshot = false;
    bool worldServicesConfigured = false;
    // world が現在の document から構築済みか。差分反映はこれが真の時だけ行う。
    bool worldBuilt = false;
    bool initialized = false;
};

} // namespace HIKARI
