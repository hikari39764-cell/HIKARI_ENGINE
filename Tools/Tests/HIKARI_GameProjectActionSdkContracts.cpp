#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include <HIKARI/Project/ActionTypes.h>
#include <HIKARI/Project/ActionViews.h>
#include <HIKARI/Project/AudioTypes.h>
#include <HIKARI/Project/AudioView.h>
#include <HIKARI/Project/CameraLensTypes.h>
#include <HIKARI/Project/CameraViewSnapshot.h>
#include <HIKARI/Project/GameProjectModuleApi.h>
#include <HIKARI/Project/GameProjectModule.h>
#include <HIKARI/Project/GameProjectSession.h>
#include <HIKARI/Project/GameProjectSdk.h>
#include <HIKARI/Project/GameComponent.h>
#include <HIKARI/Project/GameFlowTypes.h>
#include <HIKARI/Project/GameFlowViews.h>
#include <HIKARI/Project/NavigationView.h>
#include <HIKARI/Project/ComponentViews.h>
#include <HIKARI/Project/EnvironmentView.h>
#include <HIKARI/Project/LightTypes.h>
#include <HIKARI/Project/MaterialFxView.h>
#include <HIKARI/Project/PresentationTypes.h>
#include <HIKARI/Project/PresentationViews.h>
#include <HIKARI/Project/PrefabView.h>
#include <HIKARI/Project/ProjectDataView.h>
#include <HIKARI/Project/PropertyFields.h>
#include <HIKARI/Project/RuntimeActivityTypes.h>
#include <HIKARI/Project/SceneCaptureTypes.h>
#include <HIKARI/Project/SceneCaptureView.h>
#include <HIKARI/Project/SequenceTypes.h>
#include <HIKARI/Project/SequenceView.h>
#include <HIKARI/Project/SpriteAnimationTypes.h>
#include <HIKARI/Project/SpriteAnimationView.h>
#include <HIKARI/Project/SpriteMaterialTypes.h>
#include <HIKARI/Project/SpriteMaterialView.h>
#include <HIKARI/Project/SpriteShader.h>
#include <HIKARI/Project/RuntimeRegistration.h>
#include <HIKARI/Project/TerrainView.h>
#include <HIKARI/Project/UiTypes.h>
#include <HIKARI/Project/UiView.h>
#include <HIKARI/Project/ValueTypes.h>
#include <HIKARI/Project/VfxTypes.h>
#include <HIKARI/Project/VfxView.h>
#include <HIKARI/Project/WorldView.h>
#include <HIKARI/Project/WorldSpriteTypes.h>

namespace {

template <class Method>
consteval bool HasAddress(Method) noexcept {
    return true;
}

#define HIKARI_ASSERT_VALUE_TYPE(Type)                                                                                 \
    static_assert(std::is_standard_layout_v<Type>, #Type " must remain standard-layout for the public SDK.");          \
    static_assert(std::is_trivially_copyable_v<Type>, #Type " must remain trivially copyable for the public SDK.")

HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectFloat2);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectFloat4);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectQuat);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectTransform);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectInputActionState);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPointerState);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectAnimationState);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectRootMotionDelta);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectKinematicMove);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectKinematicCollisionOptions);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectKinematicMotionDelta);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectKinematicTeleport);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectKinematicState);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectSpriteAnimationState);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectSpriteMaterialState);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectSpriteMaterialParameterInfo);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPhysicsQueryFilter);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPhysicsShape);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPhysicsRaycastQuery);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPhysicsShapeCastQuery);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPhysicsOverlapQuery);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPhysicsHit);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPhysicsTriggerEvent);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectSceneEntry);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectCameraModifier);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectCameraRollModifier);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectCameraPose);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectCameraTargetFramingRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectCameraTargetFramingDynamics);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectCameraRelationshipShotProfile);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectTimeDilationRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectCombatEffectType);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectCombatWorldEffectRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectGroundTelegraphRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectWorldBarrierRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectWeaponTrailSample);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPerfectGuardWaveRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectActionCinematicGradeRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectActionBoostGradeRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectModelAssetReference);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPresentationModelAttachment);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPresentationModelRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectUiColor);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectUiTextureReference);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectUiSpriteRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectWorldSpriteState);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectWorldSpritePatch);

// Existing public value types are repeated here so the complete action
// contract cannot accidentally make one of its transitive values opaque.
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectFloat3);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectBindingValue);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectEventRecord);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectVfxAssetReference);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectVfxProfileReference);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectVfxParameterOverride);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectVfxParameterOverrides);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectVfxPlayRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectVfxUpdateRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectWeaponTrailProfiledRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectPerfectGuardWaveProfiledRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectAfterimageProfiledRequest);
HIKARI_ASSERT_VALUE_TYPE(HIKARI::GameProjectVfxPlaybackHandle);

#undef HIKARI_ASSERT_VALUE_TYPE

static_assert(std::is_same_v<std::underlying_type_t<HIKARI::GameProjectAnimationSocketSpace>, uint8_t>);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectAnimationSocketSpace::Local) == 0u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectAnimationSocketSpace::World) == 1u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectAnimationSocketSpace::PresentationWorld) == 2u);
static_assert(std::is_same_v<std::underlying_type_t<HIKARI::GameProjectRuntimeActivity>, uint8_t>);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectRuntimeActivity::Active) == 0u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectRuntimeActivity::FrozenVisible) == 1u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectRuntimeActivity::SuspendedHidden) == 2u);
static_assert(std::is_same_v<std::underlying_type_t<HIKARI::GameProjectAnimationLayerRootAlignment>, uint8_t>);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectAnimationLayerRootAlignment::ParentLocal) == 0u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectAnimationLayerRootAlignment::PreserveModelRotation) == 1u);

static_assert(sizeof(HIKARI::InputView) == sizeof(void*));
static_assert(sizeof(HIKARI::AnimationView) == sizeof(void*));
static_assert(sizeof(HIKARI::MotionView) == sizeof(void*));
static_assert(sizeof(HIKARI::PhysicsQueryView) == sizeof(void*));
static_assert(sizeof(HIKARI::CameraView) == sizeof(void*));
static_assert(sizeof(HIKARI::TimeDilationView) == sizeof(void*));
static_assert(sizeof(HIKARI::PresentationVfxView) == sizeof(void*));
static_assert(sizeof(HIKARI::PresentationModelView) == sizeof(void*));
static_assert(sizeof(HIKARI::MaterialFxView) == 48u);
static_assert(std::is_standard_layout_v<HIKARI::MaterialFxView>);
static_assert(std::is_trivially_copyable_v<HIKARI::MaterialFxView>);
static_assert(sizeof(HIKARI::VfxView) == sizeof(void*));
static_assert(sizeof(HIKARI::UiView) == sizeof(void*));
static_assert(sizeof(HIKARI::SceneFlowView) == sizeof(void*));
static_assert(sizeof(HIKARI::SaveGameView) == sizeof(void*));
static_assert(sizeof(HIKARI::RuntimeSessionView) == sizeof(void*));
static_assert(sizeof(HIKARI::WorldView) == sizeof(void*));
static_assert(sizeof(HIKARI::ProjectDataView) == sizeof(HIKARI::GameProjectWorldHandle));
static_assert(sizeof(HIKARI::NavigationView) == sizeof(void*));
static_assert(sizeof(HIKARI::TerrainView) == sizeof(void*));
static_assert(sizeof(HIKARI::GameProjectNavigationPathPoint) == 16u);
static_assert(sizeof(HIKARI::GameProjectNavigationPathResult) == 12u);
static_assert(sizeof(HIKARI::ObjectView) == sizeof(HIKARI::GameProjectObjectHandle));
static_assert(sizeof(HIKARI::GameProjectVfxAssetReference) == 608u);
static_assert(offsetof(HIKARI::GameProjectVfxAssetReference, assetPath) == 96u);
static_assert(sizeof(HIKARI::GameProjectUiTextureReference) == 608u);
static_assert(offsetof(HIKARI::GameProjectUiTextureReference, assetPath) == 96u);
static_assert(sizeof(HIKARI::GameProjectVfxParameterOverride) == 128u);
static_assert(offsetof(HIKARI::GameProjectVfxParameterOverrides, values) == 8u);
static_assert(sizeof(HIKARI::GameProjectVfxParameterOverrides) == 2056u);
static_assert(sizeof(HIKARI::GameProjectVfxProfileReference) == 704u);
static_assert(offsetof(HIKARI::GameProjectVfxProfileReference, outputId) == 608u);
static_assert(offsetof(HIKARI::GameProjectVfxPlayRequest, ownerObjectHandle) == 608u);
static_assert(offsetof(HIKARI::GameProjectVfxPlayRequest, transform) == 616u);
static_assert(offsetof(HIKARI::GameProjectVfxPlayRequest, parameters) == 664u);
static_assert(sizeof(HIKARI::GameProjectVfxPlayRequest) == 2720u);
static_assert(offsetof(HIKARI::GameProjectVfxUpdateRequest, transform) == 4u);
static_assert(offsetof(HIKARI::GameProjectVfxUpdateRequest, parameters) == 48u);
static_assert(sizeof(HIKARI::GameProjectVfxUpdateRequest) == 2104u);
static_assert(sizeof(HIKARI::GameProjectWeaponTrailProfiledRequest) == 792u);
static_assert(sizeof(HIKARI::GameProjectPerfectGuardWaveProfiledRequest) == 776u);
static_assert(sizeof(HIKARI::GameProjectAfterimageProfiledRequest) == 792u);
static_assert(offsetof(HIKARI::GameProjectVfxProfileReference, asset) == 0u);
static_assert(sizeof(HIKARI::GameProjectVfxPlaybackHandle) == 16u);
static_assert(offsetof(HIKARI::GameProjectVfxPlaybackHandle, slot) == 8u);

// The SDK ships this value through a by-reference DLL boundary. Keep
// its complete layout frozen and add optional camera channels as new
// value types/methods instead of inserting fields.
static_assert(sizeof(HIKARI::GameProjectCameraModifier) == 80u);
static_assert(offsetof(HIKARI::GameProjectCameraModifier, verticalFovOffsetRadians) == 68u);
static_assert(offsetof(HIKARI::GameProjectCameraModifier, weight) == 72u);
static_assert(offsetof(HIKARI::GameProjectCameraModifier, applyDuringOverride) == 76u);
static_assert(sizeof(HIKARI::GameProjectKinematicMove) == 72u);
static_assert(offsetof(HIKARI::GameProjectKinematicMove, horizontalVelocity) == 28u);
static_assert(offsetof(HIKARI::GameProjectKinematicMove, hasDesiredRotation) == 64u);
static_assert(sizeof(HIKARI::GameProjectKinematicCollisionOptions) == 16u);
static_assert(offsetof(HIKARI::GameProjectKinematicCollisionOptions, ignoredObjectHandle) == 0u);
static_assert(offsetof(HIKARI::GameProjectKinematicCollisionOptions, reserved0) == 8u);
static_assert(sizeof(HIKARI::GameProjectCameraRollModifier) == 32u);
static_assert(sizeof(HIKARI::GameProjectCameraTargetFramingRequest) == 80u);
static_assert(offsetof(HIKARI::GameProjectCameraTargetFramingRequest, suppressOrbitInput) == 68u);
static_assert(sizeof(HIKARI::GameProjectCameraTargetFramingDynamics) == 52u);
static_assert(offsetof(HIKARI::GameProjectCameraTargetFramingDynamics, yawDeadZoneDegrees) == 32u);
static_assert(sizeof(HIKARI::GameProjectCameraRelationshipShotProfile) == 40u);
static_assert(offsetof(HIKARI::GameProjectCameraRelationshipShotProfile, shoulderAngleDegrees) == 4u);
static_assert(offsetof(HIKARI::GameProjectCameraRelationshipShotProfile, distanceOffset) == 20u);
static_assert(offsetof(HIKARI::GameProjectCameraRelationshipShotProfile, focusHeightOffset) == 32u);
static_assert(offsetof(HIKARI::GameProjectCameraRelationshipShotProfile, suppressModifiers) == 36u);
static_assert(std::is_same_v<std::underlying_type_t<HIKARI::GameProjectCombatEffectType>, uint8_t>);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectCombatEffectType::LightImpact) == 0u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectCombatEffectType::HeavyImpact) == 1u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectCombatEffectType::SkillImpact) == 2u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectCombatEffectType::PerfectGuardBurst) == 3u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectCombatEffectType::DodgeAfterimage) == 4u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectCombatEffectType::DangerTelegraph) == 5u);
static_assert(sizeof(HIKARI::GameProjectCombatWorldEffectRequest) == 88u);
static_assert(offsetof(HIKARI::GameProjectCombatWorldEffectRequest, effect) == 80u);
static_assert(offsetof(HIKARI::GameProjectCombatWorldEffectRequest, reserved) == 81u);
static_assert(sizeof(HIKARI::GameProjectGroundTelegraphRequest) == 96u);
static_assert(offsetof(HIKARI::GameProjectGroundTelegraphRequest, shape) == 92u);
static_assert(sizeof(HIKARI::GameProjectWorldBarrierRequest) == 96u);
static_assert(sizeof(HIKARI::GameProjectWeaponTrailSample) == 88u);
static_assert(offsetof(HIKARI::GameProjectWeaponTrailSample, reserved) == 80u);
static_assert(sizeof(HIKARI::GameProjectPerfectGuardWaveRequest) == 72u);
static_assert(sizeof(HIKARI::GameProjectActionCinematicGradeRequest) == 32u);
static_assert(sizeof(HIKARI::GameProjectActionBoostGradeRequest) == 32u);
static_assert(offsetof(HIKARI::GameProjectPerfectGuardWaveRequest, flags) == 68u);
static_assert(HIKARI::GameProjectPerfectGuardWaveFlags(HIKARI::GameProjectPerfectGuardWaveFlag::Converging) == 1u);
static_assert(sizeof(HIKARI::GameProjectModelAssetReference) == 608u);
static_assert(HIKARI::kGameProjectModelAssetGuidCapacity == 96u);
static_assert(HIKARI::kGameProjectModelAssetPathCapacity == 512u);
static_assert(HIKARI::kGameProjectPresentationSocketNameCapacity == 96u);
static_assert(sizeof(HIKARI::GameProjectPresentationModelAttachment) == 140u);
static_assert(offsetof(HIKARI::GameProjectPresentationModelAttachment, socketName) == 4u);
static_assert(offsetof(HIKARI::GameProjectPresentationModelAttachment, localTransform) == 100u);
static_assert(sizeof(HIKARI::GameProjectPresentationModelRequest) == 904u);
static_assert(offsetof(HIKARI::GameProjectPresentationModelRequest, model) == 16u);
static_assert(offsetof(HIKARI::GameProjectPresentationModelRequest, worldTransform) == 624u);
static_assert(offsetof(HIKARI::GameProjectPresentationModelRequest, visible) == 664u);
static_assert(offsetof(HIKARI::GameProjectPresentationModelRequest, reserved) == 667u);
static_assert(offsetof(HIKARI::GameProjectPresentationModelRequest, materialSetAssetGuid) == 668u);
static_assert(offsetof(HIKARI::GameProjectPresentationModelRequest, attachment) == 764u);
static_assert(sizeof(HIKARI::GameProjectUiColor) == 16u);
static_assert(std::is_same_v<std::underlying_type_t<HIKARI::GameProjectUiEffect>, uint8_t>);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectUiEffect::HudIdleEnergy) == 20u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectUiEffect::TitleLogoReveal) == 21u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectUiEffect::TitleLogoIdleSheen) == 22u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectUiEffect::TitleMenuReveal) == 23u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectUiEffect::TitleMenuFocus) == 24u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectUiEffect::TitleMenuPress) == 25u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectUiEffect::TitleMenuDisabled) == 26u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectUiEffect::ItemChargeRecharge) == 27u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectUiEffect::Count) == 28u);
static_assert(sizeof(HIKARI::GameProjectUiCanvasFit) == 20u);
static_assert(sizeof(HIKARI::GameProjectSceneCaptureHandle) == 16u);
static_assert(sizeof(HIKARI::GameProjectSceneCaptureDesc) == 48u);
static_assert(sizeof(HIKARI::GameProjectSceneCaptureStyleDesc) == 140u);
static_assert(offsetof(HIKARI::GameProjectSceneCaptureStyleDesc, depthFocus) == 80u);
static_assert(offsetof(HIKARI::GameProjectSceneCaptureStyleDesc, lineColor) == 40u);
static_assert(sizeof(HIKARI::GameProjectSceneCaptureMaterialBindingDesc) == 152u);
static_assert(offsetof(HIKARI::GameProjectSceneCaptureMaterialBindingDesc, flags) == 140u);
static_assert(sizeof(HIKARI::GameProjectSceneCaptureMaterialRegionBindingDesc) == 176u);
static_assert(offsetof(HIKARI::GameProjectSceneCaptureMaterialRegionBindingDesc, sourceRect) == 144u);
static_assert(sizeof(HIKARI::GameProjectSceneCaptureSnapshot) == 64u);
static_assert(sizeof(HIKARI::GameProjectLightState) == 152u);
static_assert(sizeof(HIKARI::GameProjectLightPatch) == 144u);
static_assert(offsetof(HIKARI::GameProjectLightState, sourceRadius) == 64u);
static_assert(offsetof(HIKARI::GameProjectLightState, cookieAssetGuid) == 113u);
static_assert(offsetof(HIKARI::GameProjectLightPatch, sourceRadius) == 52u);
static_assert(offsetof(HIKARI::GameProjectLightPatch, length) == 64u);
static_assert(HIKARI::kGameProjectLightStateVersion == 4u && HIKARI::kGameProjectLightPatchVersion == 3u);
static_assert(sizeof(HIKARI::GameProjectDecalState) == 136u && sizeof(HIKARI::GameProjectDecalPatch) == 140u);
static_assert(sizeof(HIKARI::GameProjectMotionBlurSettings) == 20u);
static_assert(sizeof(HIKARI::GameProjectCameraMotionBlurRequest) == 56u);
static_assert(sizeof(HIKARI::GameProjectColorGrading) == 88u);
static_assert(static_cast<uint32_t>(HIKARI::GameProjectLightType::Spot) == 1u);
static_assert(offsetof(HIKARI::GameProjectLightState, stateRevision) == 56u);
static_assert(sizeof(HIKARI::GameProjectSequencePlaybackHandle) == 16u);
static_assert(sizeof(HIKARI::GameProjectSequenceBinding) == 80u);
static_assert(sizeof(HIKARI::GameProjectSequencePlayOptions) == 112u);
static_assert(sizeof(HIKARI::GameProjectSequencePlaybackSnapshot) == 136u);
static_assert(sizeof(HIKARI::GameProjectSequencePlaybackEvent) == 328u);
static_assert(sizeof(HIKARI::GameProjectUiSpriteRequest) == 136u);
static_assert(offsetof(HIKARI::GameProjectUiSpriteRequest, referenceWidth) == 16u);
static_assert(offsetof(HIKARI::GameProjectUiSpriteRequest, fillAmount) == 52u);
static_assert(offsetof(HIKARI::GameProjectUiSpriteRequest, effectColor) == 92u);
static_assert(offsetof(HIKARI::GameProjectUiSpriteRequest, effect) == 124u);

constexpr HIKARI::GameProjectCombatWorldEffectRequest kDefaultCombatWorldEffectRequest{};
constexpr HIKARI::GameProjectWeaponTrailSample kDefaultWeaponTrailSample{};
constexpr HIKARI::GameProjectPerfectGuardWaveRequest kDefaultPerfectGuardWaveRequest{};
constexpr HIKARI::GameProjectVfxUpdateRequest kDefaultVfxUpdateRequest{};
static_assert(kDefaultCombatWorldEffectRequest.reserved[0] == 0u);
static_assert(kDefaultCombatWorldEffectRequest.reserved[1] == 0u);
static_assert(kDefaultCombatWorldEffectRequest.reserved[2] == 0u);
static_assert(kDefaultWeaponTrailSample.reserved == 0u);
static_assert(kDefaultPerfectGuardWaveRequest.flags == 0u);
static_assert(kDefaultVfxUpdateRequest.updateTransform == 0u);
static_assert(kDefaultVfxUpdateRequest.updateParameters == 0u);
constexpr HIKARI::GameProjectPresentationModelRequest kDefaultPresentationModelRequest{};
static_assert(kDefaultPresentationModelRequest.attachment.enabled == 0u);
static_assert(kDefaultPresentationModelRequest.attachment.reserved[0] == 0u);
static_assert(kDefaultPresentationModelRequest.attachment.reserved[1] == 0u);
static_assert(kDefaultPresentationModelRequest.attachment.reserved[2] == 0u);

static_assert(HIKARI::RUNTIME::kGameProjectModuleAbiVersion == 8u);
static_assert(HasAddress(&HIKARI::RUNTIME::IGameProjectModule::CreateGameSession));
static_assert(HasAddress(&HIKARI::IGameProjectSession::OnGameStarted));
static_assert(HasAddress(&HIKARI::IGameProjectSession::OnSceneEntered));
static_assert(HasAddress(&HIKARI::IGameProjectSession::OnSceneExiting));
static_assert(HasAddress(&HIKARI::IGameProjectSession::OnGameStopped));

static_assert(sizeof(HIKARI::GameProjectWorldSpriteState) == HIKARI::kGameProjectWorldSpriteStateV1Size);
static_assert(sizeof(HIKARI::GameProjectWorldSpritePatch) == HIKARI::kGameProjectWorldSpritePatchV1Size);
static_assert(offsetof(HIKARI::GameProjectWorldSpriteState, flipX) == 66u);
static_assert(offsetof(HIKARI::GameProjectWorldSpriteState, reserved) == 67u);
static_assert(offsetof(HIKARI::GameProjectWorldSpritePatch, flipX) == 66u);
static_assert(offsetof(HIKARI::GameProjectWorldSpritePatch, reserved1) == 67u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectWorldResultCode::InvalidAsset) == 16u);
static_assert(sizeof(HIKARI::WorldView) == sizeof(void*));
static_assert(sizeof(HIKARI::GameProjectSpriteAnimationState) == 32u);
static_assert(sizeof(HIKARI::GameProjectSpriteMaterialState) == 152u);
static_assert(sizeof(HIKARI::GameProjectSpriteMaterialParameterInfo) == 448u);
static_assert(offsetof(HIKARI::GameProjectSpriteMaterialState, stateRevision) == 144u);
static_assert(offsetof(HIKARI::GameProjectSpriteMaterialParameterInfo, defaultValue) == 416u);
static_assert(sizeof(HIKARI::SpriteMaterialView) == sizeof(HIKARI::GameProjectObjectHandle));
static_assert(sizeof(HIKARI::GameProjectWorldQueryTerm) == 24u);
static_assert(sizeof(HIKARI::GameProjectWorldQueryDesc) == 56u);
static_assert(sizeof(HIKARI::GameProjectWorldQueryRow) == 48u);
static_assert(sizeof(HIKARI::GameProjectWorldQueryResult) == 32u);
static_assert(sizeof(HIKARI::GameProjectMassDataTypeDesc) == 40u);
static_assert(sizeof(HIKARI::GameProjectWorldMassRenderProxyDesc) == 160u);
static_assert(sizeof(HIKARI::GameProjectWorldMassSpawnBatchDesc) == 72u);
static_assert(sizeof(HIKARI::GameProjectWorldMassRow) == 80u);
static_assert(sizeof(HIKARI::GameProjectMassSystemAccessDesc) == 32u);
static_assert(sizeof(HIKARI::GameProjectMassSystemScheduleDesc) == 72u);
static_assert(sizeof(HIKARI::GameProjectMassPartitionInput) == 120u);
static_assert(sizeof(HIKARI::GameProjectMassPartitionOutput) == 80u);
static_assert(sizeof(HIKARI::GameProjectMassCommandWriter) == 32u);
static_assert(std::is_same_v<std::underlying_type_t<HIKARI::GameProjectMassSystemPhase>, uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<HIKARI::GameProjectModelNodeVisibilityScope>, uint32_t>);
static_assert(static_cast<uint32_t>(HIKARI::GameProjectModelNodeVisibilityScope::NodeOnly) == 0u);
static_assert(static_cast<uint32_t>(HIKARI::GameProjectModelNodeVisibilityScope::NodeAndDescendants) == 1u);
static_assert(static_cast<uint32_t>(HIKARI::GameProjectModelNodeVisibilityScope::Count) == 2u);
static_assert(std::is_same_v<std::underlying_type_t<HIKARI::GameProjectMassSystemField>, uint32_t>);
static_assert(std::is_standard_layout_v<HIKARI::GameProjectMassSystemScheduleDesc>);
static_assert(std::is_trivially_copyable_v<HIKARI::GameProjectMassSystemScheduleDesc>);
static_assert(std::is_standard_layout_v<HIKARI::GameProjectMassPartitionInput>);
static_assert(std::is_trivially_copyable_v<HIKARI::GameProjectMassPartitionInput>);
static_assert(sizeof(HIKARI::GameSystemDescriptor) == 48u);
static_assert(sizeof(HIKARI::RuntimeFeatureContext) == 48u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectWorldCommandKind::ParallelMassSystem) == 5u);
static_assert(sizeof(HIKARI::GameProjectRenderFrameToken) == 24u);
static_assert(sizeof(HIKARI::GameProjectRenderSurfaceHandle) == 48u);
static_assert(sizeof(HIKARI::GameProjectRenderSurfaceDesc) == 96u);
static_assert(sizeof(HIKARI::GameProjectRenderCameraFrame) == 496u);
static_assert(sizeof(HIKARI::GameProjectRenderFrameView) == 120u);
static_assert(sizeof(HIKARI::GameProjectRenderObserverDesc) == 32u);
static_assert(static_cast<uint16_t>(HIKARI::GameProjectRenderSurfaceSemantic::SceneDepth) == 1u);
static_assert(static_cast<uint16_t>(HIKARI::GameProjectRenderSurfaceSemantic::InvalidDepthMotionMask) == 17u);
static_assert(static_cast<uint16_t>(HIKARI::GameProjectRenderSurfaceSemantic::ProjectDispatchOutput) == 18u);
static_assert(static_cast<uint8_t>(HIKARI::GameProjectRenderSurfacePhase::PostOpaque) == 4u);
static_assert(std::is_standard_layout_v<HIKARI::GameProjectRenderFrameView>);
static_assert(std::is_trivially_copyable_v<HIKARI::GameProjectRenderFrameView>);
static_assert(sizeof(HIKARI::GameProjectRenderPassResourceUse) == 16u);
static_assert(sizeof(HIKARI::GameProjectRenderPassDesc) == 224u);
static_assert(sizeof(HIKARI::GameProjectRenderCommandEncoder) == 56u);
static_assert(sizeof(HIKARI::GameProjectRenderPassContext) == 88u);
static_assert(sizeof(HIKARI::GameProjectShaderProgramRef) == 96u);
static_assert(sizeof(HIKARI::GameProjectFullscreenDrawDesc) == 132u);
static_assert(HIKARI::IsGameProjectFullscreenShaderInput(HIKARI::GameProjectRenderSurfaceSemantic::SceneColorOpaque,
                                                         HIKARI::GameProjectRenderSurfacePhase::Opaque));
static_assert(HIKARI::IsGameProjectFullscreenShaderInput(HIKARI::GameProjectRenderSurfaceSemantic::SceneColorFinal,
                                                         HIKARI::GameProjectRenderSurfacePhase::Final));
static_assert(!HIKARI::IsGameProjectFullscreenShaderInput(
    HIKARI::GameProjectRenderSurfaceSemantic::SceneColorPostOpaque, HIKARI::GameProjectRenderSurfacePhase::PostOpaque));
static_assert(sizeof(HIKARI::GameProjectComputeShaderProgramRef) == 96u);
static_assert(sizeof(HIKARI::GameProjectComputeDispatchDesc) == 128u);
static_assert(offsetof(HIKARI::GameProjectRenderPassDesc, resources) == 76u);
static_assert(offsetof(HIKARI::GameProjectRenderPassContext, encoder) == 16u);
static_assert(offsetof(HIKARI::GameProjectShaderProgramRef, pixelEntryId) == 41u);
static_assert(offsetof(HIKARI::GameProjectRenderCommandEncoder, drawFullscreen) == 40u);
static_assert(offsetof(HIKARI::GameProjectRenderCommandEncoder, dispatchCompute) == 48u);
static_assert(offsetof(HIKARI::GameProjectComputeShaderProgramRef, computeEntryId) == 41u);
static_assert(static_cast<uint32_t>(HIKARI::GameProjectRenderPassTarget::WorkingSceneColor) == 1u);
static_assert(static_cast<uint32_t>(HIKARI::GameProjectRenderPassTarget::ProjectDispatchOutput) == 2u);
static_assert(std::is_standard_layout_v<HIKARI::GameProjectRenderPassDesc>);
static_assert(std::is_trivially_copyable_v<HIKARI::GameProjectRenderPassContext>);
static_assert(offsetof(HIKARI::GameProjectMassDataTypeDesc, layoutHash) == 16u);
static_assert(offsetof(HIKARI::GameProjectWorldMassRenderProxyDesc, modelAssetGuid) == 40u);
static_assert(offsetof(HIKARI::GameProjectWorldMassRenderProxyDesc, meshIndex) == 136u);
static_assert(offsetof(HIKARI::GameProjectWorldMassSpawnBatchDesc, podDataByteCount) == 40u);
static_assert(offsetof(HIKARI::GameProjectWorldMassRow, podData) == 56u);
static_assert(sizeof(HIKARI::GameProjectWorldTransformWrite) == 72u);
static_assert(HIKARI::kGameProjectWorldMaximumTransformWriteBatchCount == 1024u);
static_assert(offsetof(HIKARI::GameProjectWorldTransformWrite, object) == 8u);
static_assert(offsetof(HIKARI::GameProjectWorldTransformWrite, localTransform) == 24u);
static_assert(offsetof(HIKARI::GameProjectWorldTransformWrite, reserved) == 64u);
static_assert(sizeof(HIKARI::GameProjectSpawnComponentDesc) == 40u);
static_assert(sizeof(HIKARI::GameProjectSpawnObjectComponentsDesc) == 232u);
static_assert(offsetof(HIKARI::GameProjectSpawnComponentDesc, initialValues) == 16u);
static_assert(offsetof(HIKARI::GameProjectSpawnObjectComponentsDesc, components) == 208u);
static_assert(std::is_same_v<std::underlying_type_t<HIKARI::GameProjectSpawnComponentValuesFormat>, uint32_t>);
static_assert(HasAddress(&HIKARI::WorldView::QueryComponents));
static_assert(HasAddress(&HIKARI::WorldView::SpawnObjectsWithComponents));
static_assert(HasAddress(&HIKARI::WorldView::SetLocalTransforms));
static_assert(HasAddress(&HIKARI::WorldView::GetMassObjectCount));
static_assert(HasAddress(&HIKARI::WorldView::SpawnMassObjects));
static_assert(HasAddress(&HIKARI::WorldView::DestroyMassObjects));
static_assert(HasAddress(&HIKARI::WorldView::QueryMassObjects));

// Taking each address in a constant expression forces overloads and
// parameter types to resolve while keeping this fixture link-independent.
static_assert(HasAddress(&HIKARI::PropertyFields::Section));
static_assert(HasAddress(&HIKARI::PropertyFields::TextField));
static_assert(HasAddress(&HIKARI::PropertyFields::SceneObject));
static_assert(HasAddress(&HIKARI::PropertyFields::Color3));
static_assert(HasAddress(&HIKARI::PropertyFields::Vfx));
static_assert(HasAddress(&HIKARI::PropertyFields::Texture));
using RangedFloat = bool (HIKARI::PropertyFields::*)(std::string_view, std::string_view, float&, float, float, float);
static_assert(HasAddress(static_cast<RangedFloat>(&HIKARI::PropertyFields::Float)));
using NamedChoice = bool (HIKARI::PropertyFields::*)(std::string_view, std::string_view, int&,
                                                     std::span<const std::string_view>);
static_assert(HasAddress(static_cast<NamedChoice>(&HIKARI::PropertyFields::Choice)));
static_assert(HasAddress(&HIKARI::InputView::GetAction));
static_assert(HasAddress(&HIKARI::InputView::GetLastActiveDevice));
static_assert(HasAddress(&HIKARI::InputView::GetFrameIndex));
static_assert(HasAddress(&HIKARI::InputView::GetPointerState));
static_assert(HasAddress(&HIKARI::InputView::SetContextActive));
static_assert(HasAddress(&HIKARI::AnimationView::SetNodeParentLocalTransformOffset));
static_assert(HasAddress(&HIKARI::InputView::SetUiInteractionActive));
static_assert(HasAddress(&HIKARI::InputView::SetGamepadVibration));
static_assert(HasAddress(&HIKARI::InputView::StopGamepadVibration));

static_assert(HasAddress(&HIKARI::AnimationView::Play));
static_assert(HasAddress(&HIKARI::AnimationView::CrossFade));
static_assert(HasAddress(&HIKARI::AnimationView::PlayOnce));
static_assert(HasAddress(&HIKARI::AnimationView::Pause));
static_assert(HasAddress(&HIKARI::AnimationView::Resume));
static_assert(HasAddress(&HIKARI::AnimationView::Stop));
static_assert(HasAddress(&HIKARI::AnimationView::SetSpeed));
static_assert(HasAddress(&HIKARI::AnimationView::SetEvaluationEnabled));
static_assert(HasAddress(&HIKARI::AnimationView::SetMaskedPoseLayer));
static_assert(HasAddress(&HIKARI::AnimationView::SetMaskedPoseLayerWithRootAlignment));
static_assert(HasAddress(&HIKARI::AnimationView::ClearMaskedPoseLayer));
static_assert(HasAddress(&HIKARI::AnimationView::SetRootMotionEnabled));
static_assert(HasAddress(&HIKARI::AnimationView::GetState));
static_assert(HasAddress(&HIKARI::AnimationView::GetSocketTransform));
static_assert(HasAddress(&HIKARI::AnimationView::SetNodeLocalTransformOffset));
static_assert(HasAddress(&HIKARI::AnimationView::ClearNodeLocalTransformOffset));
static_assert(HasAddress(&HIKARI::AnimationView::SampleSocketTransformsAtTime));
static_assert(HasAddress(&HIKARI::AnimationView::SampleRootMotionDeltaAtTime));
static_assert(HasAddress(&HIKARI::AnimationView::ConsumeRootMotion));

static_assert(HasAddress(&HIKARI::MotionView::SubmitKinematicMove));
static_assert(HasAddress(&HIKARI::MotionView::SubmitKinematicMoveWithCollisionOptions));
static_assert(HasAddress(&HIKARI::MotionView::SubmitMotionDelta));
static_assert(HasAddress(&HIKARI::MotionView::RequestTeleport));
static_assert(HasAddress(&HIKARI::MotionView::GetKinematicState));

static_assert(HasAddress(&HIKARI::PhysicsQueryView::Raycast));
static_assert(HasAddress(&HIKARI::PhysicsQueryView::ShapeCast));
static_assert(HasAddress(&HIKARI::PhysicsQueryView::Overlap));
static_assert(HasAddress(&HIKARI::PhysicsQueryView::ReadTriggerEvents));

static_assert(HasAddress(&HIKARI::CameraView::SubmitModifier));
static_assert(HasAddress(&HIKARI::CameraView::SubmitRollModifier));
static_assert(HasAddress(&HIKARI::CameraView::GetAuthoredLens));
static_assert(HasAddress(&HIKARI::CameraView::GetControlLens));
static_assert(HasAddress(&HIKARI::CameraView::GetRenderedLens));
static_assert(HasAddress(&HIKARI::CameraView::SubmitDepthOfField));
static_assert(HasAddress(&HIKARI::CameraView::SubmitFocus));
static_assert(HasAddress(&HIKARI::CameraView::TryGetViewSnapshot));
static_assert(HasAddress(&HIKARI::ProjectWorldToScreen));
static_assert(HasAddress(&HIKARI::CameraView::GetControlPose));
static_assert(HasAddress(&HIKARI::CameraView::IsReady));
static_assert(HasAddress(&HIKARI::CameraView::SubmitTargetFraming));
static_assert(HasAddress(&HIKARI::CameraView::SubmitTargetFramingWithDynamics));
static_assert(HasAddress(&HIKARI::CameraView::SubmitRelationshipShot));

static_assert(HIKARI::SDK::kVersionMajor == 4u && HIKARI::SDK::kVersionMinor == 6u && HIKARI::SDK::kVersionPatch == 0u);
static_assert(sizeof(HIKARI::GameProjectKinematicMoveOptions) == 32u);
static_assert(offsetof(HIKARI::GameProjectKinematicMoveOptions, collision) == 8u);
static_assert(offsetof(HIKARI::GameProjectKinematicMoveOptions, verticalGravityAcceleration) == 24u);
static_assert(offsetof(HIKARI::GameProjectKinematicMoveOptions, overrideVerticalGravity) == 28u);
static_assert(std::is_standard_layout_v<HIKARI::GameProjectKinematicMoveOptions>);
static_assert(std::is_trivially_copyable_v<HIKARI::GameProjectKinematicMoveOptions>);
static_assert(HasAddress(&HIKARI::MotionView::SubmitKinematicMoveWithOptions));
static_assert(sizeof(HIKARI::GameProjectDepthOfFieldSettings) == 36u);
static_assert(sizeof(HIKARI::GameProjectCameraLens) == 80u);
static_assert(sizeof(HIKARI::GameProjectCameraDepthOfFieldRequest) == 72u);
static_assert(sizeof(HIKARI::GameProjectCameraFocusRequest) == 80u);
static_assert(sizeof(HIKARI::GameProjectCameraViewSnapshot) == 216u);
static_assert(sizeof(HIKARI::GameProjectWorldToScreenResult) == 40u);

static_assert(HasAddress(&HIKARI::TimeDilationView::Request));
static_assert(HasAddress(&HIKARI::TimeDilationView::Release));
static_assert(HasAddress(&HIKARI::TimeDilationView::GetEffectiveScale));
static_assert(HasAddress(&HIKARI::PresentationVfxView::PlayWorldEffect));
static_assert(HasAddress(&HIKARI::PresentationVfxView::SubmitGroundTelegraph));
static_assert(HasAddress(&HIKARI::PresentationVfxView::SubmitWorldBarrier));
static_assert(HasAddress(&HIKARI::PresentationVfxView::SubmitActionCinematicGrade));
static_assert(HasAddress(&HIKARI::PresentationVfxView::SubmitActionBoostGrade));
static_assert(HasAddress(&HIKARI::PresentationVfxView::SubmitWeaponTrailProfiled));
static_assert(HasAddress(&HIKARI::PresentationVfxView::TriggerPerfectGuardWaveProfiled));
static_assert(HasAddress(&HIKARI::PresentationVfxView::CaptureAfterimageProfiled));
static_assert(HasAddress(&HIKARI::PresentationVfxView::SetAfterimagePaused));
static_assert(HasAddress(&HIKARI::PresentationVfxView::ClearAfterimages));
static_assert(HasAddress(&HIKARI::PresentationModelView::Submit));
static_assert(HasAddress(&HIKARI::PresentationModelView::Remove));
static_assert(HasAddress(&HIKARI::PresentationModelView::GetMaterialFx));
static_assert(HasAddress(&HIKARI::MaterialFxView::SetProfileId));
static_assert(HasAddress(&HIKARI::MaterialFxView::SetFloat));
static_assert(HasAddress(&HIKARI::MaterialFxView::SetFloat4));
static_assert(HasAddress(&HIKARI::MaterialFxView::SetToggle));
static_assert(HasAddress(&HIKARI::MaterialFxView::TryGetFloat));
static_assert(HasAddress(&HIKARI::VfxView::Play));
static_assert(HasAddress(&HIKARI::VfxView::Stop));
static_assert(HasAddress(&HIKARI::VfxView::Update));
static_assert(HasAddress(&HIKARI::VfxView::IsActive));
static_assert(HasAddress(&HIKARI::UiView::SubmitSprite));
static_assert(HasAddress(&HIKARI::UiView::SubmitText));
static_assert(HasAddress(&HIKARI::UiView::SubmitBackdrop));
static_assert(HasAddress(&HIKARI::UiView::WasActivated));
static_assert(HasAddress(&HIKARI::SequenceView::Play));
static_assert(HasAddress(&HIKARI::SequenceView::SetPlaybackRate));
static_assert(HasAddress(&HIKARI::SequenceView::GetSnapshot));
static_assert(HasAddress(&HIKARI::SequenceView::GetEventClips));
static_assert(HasAddress(&HIKARI::SequenceView::GetTextClips));
static_assert(HasAddress(&HIKARI::SequenceView::GetTextClipText));
static_assert(HasAddress(&HIKARI::SequenceView::GetEditorPreview));
static_assert(sizeof(HIKARI::GameProjectSequenceTextClip) == 192u);
static_assert(HasAddress(&HIKARI::SceneCaptureView::Create));
static_assert(HasAddress(&HIKARI::SceneCaptureView::SetStyle));
static_assert(HasAddress(&HIKARI::SceneCaptureView::GetSnapshot));
static_assert(HasAddress(&HIKARI::SceneCaptureView::BindMaterialRegion));
static_assert(HasAddress(&HIKARI::WorldView::GetSequence));
static_assert(HasAddress(&HIKARI::WorldView::GetPrefabs));
static_assert(HasAddress(&HIKARI::PrefabView::RequestInstantiate));
static_assert(HasAddress(&HIKARI::PrefabView::ReadRequestResult));
static_assert(HasAddress(&HIKARI::PrefabView::CancelRequest));
static_assert(HasAddress(&HIKARI::PrefabView::ReleaseRequest));
static_assert(HasAddress(&HIKARI::PrefabView::FindInstanceObject));
static_assert(HasAddress(&HIKARI::PrefabView::DestroyInstance));
static_assert(HasAddress(&HIKARI::WorldView::GetSceneCapture));
static_assert(HasAddress(&HIKARI::ObjectView::GetLight));
static_assert(HasAddress(&HIKARI::LightView::TryGetState));
static_assert(HasAddress(&HIKARI::LightView::ApplyPatch));
static_assert(HasAddress(&HIKARI::ObjectView::GetDecal));
static_assert(HasAddress(&HIKARI::DecalView::TryGetState));
static_assert(HasAddress(&HIKARI::DecalView::ApplyPatch));
static_assert(HasAddress(&HIKARI::CameraView::GetAuthoredMotionBlur));
static_assert(HasAddress(&HIKARI::CameraView::SubmitMotionBlur));
static_assert(HasAddress(&HIKARI::EnvironmentView::GetColorGrading));
static_assert(HasAddress(&HIKARI::EnvironmentView::SetColorGradingOverride));
static_assert(HasAddress(&HIKARI::EnvironmentView::ClearColorGradingOverride));
static_assert(HasAddress(&HIKARI::ObjectView::GetWorldSprite));
static_assert(HasAddress(&HIKARI::WorldSpriteView::TryGetState));
static_assert(HasAddress(&HIKARI::WorldSpriteView::ApplyPatch));
static_assert(HasAddress(&HIKARI::ObjectView::GetWorldText));
static_assert(HasAddress(&HIKARI::WorldTextView::SetContent));
static_assert(HasAddress(&HIKARI::ObjectView::GetSpriteAnimation));
static_assert(HasAddress(&HIKARI::ObjectView::GetSpriteMaterial));
static_assert(HasAddress(&HIKARI::SpriteMaterialView::BindAsset));
static_assert(HasAddress(&HIKARI::SpriteMaterialView::ClearBinding));
static_assert(HasAddress(&HIKARI::SpriteMaterialView::SetEnabled));
static_assert(HasAddress(&HIKARI::SpriteMaterialView::SetParameter));
static_assert(HasAddress(&HIKARI::SpriteMaterialView::ResetParameter));
static_assert(HasAddress(&HIKARI::SpriteMaterialView::ResetParameters));
static_assert(HasAddress(&HIKARI::SpriteMaterialView::TryGetState));
static_assert(HasAddress(&HIKARI::SpriteMaterialView::TryGetParameter));
static_assert(HasAddress(&HIKARI::SpriteMaterialView::TryGetParameterInfo));
static_assert(HasAddress(&HIKARI::ObjectView::GetMaterialFx));
static_assert(HasAddress(&HIKARI::SpriteAnimationView::Bind));
static_assert(HasAddress(&HIKARI::SpriteAnimationView::Play));
static_assert(HasAddress(&HIKARI::SpriteAnimationView::Pause));
static_assert(HasAddress(&HIKARI::SpriteAnimationView::Resume));
static_assert(HasAddress(&HIKARI::SpriteAnimationView::Restart));
static_assert(HasAddress(&HIKARI::SpriteAnimationView::SetSpeed));
static_assert(HasAddress(&HIKARI::SpriteAnimationView::Seek));
static_assert(HasAddress(&HIKARI::SpriteAnimationView::TryGetState));
static_assert(HasAddress(&HIKARI::ObjectView::GetModel));
static_assert(HasAddress(&HIKARI::ModelView::SetMaterialColorOverride));
static_assert(HasAddress(&HIKARI::ModelView::SetNodeVisibility));
static_assert(HasAddress(&HIKARI::ModelView::ClearNodeVisibility));
static_assert(HasAddress(&HIKARI::ModelView::ClearAllNodeVisibility));
static_assert(HasAddress(&HIKARI::SceneFlowView::GetCurrentSceneAssetGuid));
static_assert(HasAddress(&HIKARI::SceneFlowView::RequestScene));
static_assert(HasAddress(&HIKARI::SceneFlowView::RequestSceneWithHeldReveal));
static_assert(HasAddress(&HIKARI::SceneFlowView::ReleaseTransitionIn));
static_assert(HasAddress(&HIKARI::SceneFlowView::ConsumeSceneEntry));
static_assert(HasAddress(&HIKARI::SceneFlowView::IsTransitioning));
static_assert(HasAddress(&HIKARI::DialogueView::Start));
static_assert(HasAddress(&HIKARI::DialogueView::IsActive));
static_assert(HasAddress(&HIKARI::DialogueView::Stop));
static_assert(HasAddress(&HIKARI::DialogueView::CaptureOneShotTriggerMask));
static_assert(HasAddress(&HIKARI::DialogueView::RestoreOneShotTriggerMask));
static_assert(HasAddress(&HIKARI::AudioView::SetBusVolume));
static_assert(HasAddress(&HIKARI::AudioView::GetBusVolume));
static_assert(HasAddress(&HIKARI::AudioView::Load));
static_assert(requires(const HIKARI::AudioView& audio, HIKARI::GameProjectAudioSoundHandle sound,
                       HIKARI::GameProjectAudioBus bus) {
    { audio.Play(sound, bus, uint8_t{}, 1.0f) } -> std::same_as<HIKARI::GameProjectAudioPlaybackHandle>;
});
static_assert(HasAddress(&HIKARI::AudioView::Stop));
static_assert(HasAddress(&HIKARI::AudioView::Pause));
static_assert(HasAddress(&HIKARI::AudioView::Resume));
static_assert(HasAddress(&HIKARI::AudioView::SetVolume));
static_assert(HasAddress(&HIKARI::AudioView::IsPlaying));
static_assert(HasAddress(&HIKARI::SaveGameView::Exists));
static_assert(HasAddress(&HIKARI::SaveGameView::GetSize));
static_assert(HasAddress(&HIKARI::SaveGameView::Read));
static_assert(HasAddress(&HIKARI::SaveGameView::WriteAtomic));
static_assert(HasAddress(&HIKARI::SaveGameView::Delete));
static_assert(HasAddress(&HIKARI::RuntimeSessionView::GetHostKind));
static_assert(HasAddress(&HIKARI::RuntimeSessionView::IsPlayActive));
static_assert(HasAddress(&HIKARI::RuntimeSessionView::RequestEndSession));

static_assert(HasAddress(&HIKARI::WorldView::GetInput));
static_assert(HasAddress(&HIKARI::WorldView::GetAudio));
static_assert(std::is_standard_layout_v<HIKARI::GameProjectAudioPlaybackSnapshot>);
static_assert(std::is_trivially_copyable_v<HIKARI::GameProjectAudioPlaybackSnapshot>);
static_assert(sizeof(HIKARI::GameProjectAudioPlaybackSnapshot) == 64u);
static_assert(offsetof(HIKARI::GameProjectAudioPlaybackSnapshot, positionSeconds) == 16u);
static_assert(offsetof(HIKARI::GameProjectAudioPlaybackSnapshot, completedLoops) == 40u);
static_assert(HasAddress(&HIKARI::AudioView::TryGetPlaybackSnapshot));
static_assert(HasAddress(&HIKARI::ModelView::SetPostGroupMask));
static_assert(HasAddress(&HIKARI::ModelView::ClearPostGroupMask));
static_assert(HasAddress(&HIKARI::VfxView::SetTimeScale));
static_assert(HasAddress(&HIKARI::WorldView::GetAnimation));
static_assert(HasAddress(&HIKARI::WorldView::GetMotion));
static_assert(HasAddress(&HIKARI::WorldView::GetPhysicsQueries));
static_assert(HasAddress(&HIKARI::WorldView::GetNavigation));
static_assert(HasAddress(&HIKARI::WorldView::GetTerrain));
static_assert(HasAddress(&HIKARI::WorldView::GetCamera));
static_assert(HasAddress(&HIKARI::WorldView::GetTimeDilation));
static_assert(HasAddress(&HIKARI::WorldView::GetPresentationModels));
static_assert(HasAddress(&HIKARI::WorldView::GetPresentationVfx));
static_assert(HasAddress(&HIKARI::WorldView::GetVfx));
static_assert(HasAddress(&HIKARI::WorldView::GetUi));
static_assert(HasAddress(&HIKARI::WorldView::GetSceneFlow));
static_assert(HasAddress(&HIKARI::WorldView::GetDialogue));
static_assert(HasAddress(&HIKARI::WorldView::GetSaveGame));
static_assert(HasAddress(&HIKARI::WorldView::GetRuntimeSession));
static_assert(HasAddress(&HIKARI::WorldView::GetGameSession));
static_assert(HasAddress(&HIKARI::WorldView::GetProjectData));
static_assert(HasAddress(&HIKARI::ProjectDataView::ReadText));
static_assert(HasAddress(&HIKARI::NavigationView::ProjectPoint));
static_assert(HasAddress(&HIKARI::NavigationView::FindPath));
static_assert(HasAddress(&HIKARI::NavigationView::Raycast));
static_assert(HasAddress(&HIKARI::NavigationView::GetSourceRevision));
static_assert(HasAddress(&HIKARI::TerrainView::IsTerrainSurfaceAt));
static_assert(HasAddress(&HIKARI::RuntimeFeatureContext::RegisterMassRenderProxy));

// The two existing view surfaces are also included to catch accidental
// ABI changes caused by the action-view composition.
static_assert(HasAddress(&HIKARI::ObjectView::GetHandle));
static_assert(HasAddress(&HIKARI::ObjectView::GetName));
static_assert(HasAddress(&HIKARI::ObjectView::GetLocalTransform));
static_assert(HasAddress(&HIKARI::ObjectView::GetWorldTransform));
static_assert(HasAddress(&HIKARI::ObjectView::SetLocalTransform));
static_assert(HasAddress(&HIKARI::WorldView::FindObject));
static_assert(HasAddress(&HIKARI::WorldView::ReadVariable));
static_assert(HasAddress(&HIKARI::WorldView::ReadObjectVariable));
static_assert(HasAddress(&HIKARI::WorldView::WriteVariable));
static_assert(HasAddress(&HIKARI::WorldView::ReleaseVariableWriter));
static_assert(HasAddress(&HIKARI::GameComponentContext::ReadVariable));
static_assert(HasAddress(&HIKARI::GameComponentContext::ReadObjectVariable));
static_assert(HasAddress(&HIKARI::GameComponentContext::WriteVariable));
static_assert(HasAddress(&HIKARI::GameComponentContext::ReleaseVariable));
static_assert(HasAddress(&HIKARI::GameComponentContext::WriteObjectVariable));
static_assert(HasAddress(&HIKARI::GameComponentContext::ReleaseObjectVariable));
static_assert(HasAddress(static_cast<bool (HIKARI::WorldView::*)(std::string_view, HIKARI::GameProjectObjectHandle)
                                         const noexcept>(&HIKARI::WorldView::EmitEvent)));
static_assert(HasAddress(static_cast<bool (HIKARI::WorldView::*)(std::string_view, HIKARI::GameProjectObjectHandle,
                                                                 const HIKARI::GameProjectBindingValue&)
                                         const noexcept>(&HIKARI::WorldView::EmitEvent)));
static_assert(HasAddress(&HIKARI::WorldView::GetEventCount));
static_assert(HasAddress(&HIKARI::WorldView::ReadEvent));

} // namespace

static_assert(std::is_standard_layout_v<HIKARI::GameProjectActionPostFrameRequest>);
static_assert(std::is_trivially_copyable_v<HIKARI::GameProjectActionPostFrameRequest>);
static_assert(sizeof(HIKARI::GameProjectActionPostFrameRequest) == 344u);
static_assert(offsetof(HIKARI::GameProjectActionPostFrameRequest, sourceId) == 8u);
static_assert(offsetof(HIKARI::GameProjectActionPostFrameRequest, values) == 24u);
static_assert(offsetof(HIKARI::GameProjectActionPostFrameRequest, channelMasks) == 280u);
static_assert(HasAddress(&HIKARI::PresentationVfxView::SubmitActionPostProfile));
static_assert(HasAddress(&HIKARI::PresentationVfxView::ClearActionPost));

int main() noexcept {
    // These inline calls make the executable exercise every view's invalid
    // default state without requiring a host SDK import library.
    const HIKARI::InputView input{};
    const HIKARI::AnimationView animation{};
    const HIKARI::MotionView motion{};
    const HIKARI::PhysicsQueryView physics{};
    const HIKARI::NavigationView navigation{};
    const HIKARI::CameraView camera{};
    const HIKARI::TimeDilationView time{};
    const HIKARI::PresentationModelView presentationModels{};
    const HIKARI::PresentationVfxView presentationVfx{};
    const HIKARI::VfxView vfx{};
    const HIKARI::UiView ui{};
    const HIKARI::WorldView world{};
    const HIKARI::ProjectDataView projectData{};
    const HIKARI::ObjectView object{};
    const HIKARI::GameProjectModelAssetReference emptyModel{};

    return input.IsValid() || animation.IsValid() || motion.IsValid() || physics.IsValid() || navigation.IsValid() ||
                   camera.IsValid() || time.IsValid() || presentationModels.IsValid() || presentationVfx.IsValid() ||
                   vfx.IsValid() || ui.IsValid() || world.IsValid() || projectData.IsValid() || object.IsValid() ||
                   !emptyModel.IsEmpty() || emptyModel.HasGuid() || emptyModel.HasPath()
               ? 1
               : 0;
}
