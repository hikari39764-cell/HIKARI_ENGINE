#pragma once

#include <memory>

#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Animation/StateMachine/HIKARI_AnimationStateMachineRuntimeService.h"
#include "Assets/Animation/HIKARI_AnimationStateMachineAssetStore.h"
#include "Core/HIKARI_FixedStepClock.h"
#include "Gameplay/Motion/HIKARI_CharacterMotionStateService.h"
#include "Gameplay/Motion/HIKARI_MotionIntentService.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsCollisionGeometryStore.h"
#include "Physics/HIKARI_PhysicsProjectSettings.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Render3D/Material/Program/HIKARI_MaterialParameterProjection.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Features/HIKARI_RuntimeExtension.h"
#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"
#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI {

    struct DocumentSceneRuntimeState {
        World world{};
        FixedStepClock fixedStepClock{};
        RuntimePlayStateService playStateService{};
        GameplayCameraService gameplayCameraService{};
        ANIMATION::AnimationPoseService animationPoseService{};
        ANIMATION::AnimationStateMachineRuntimeService
            animationStateMachineRuntimeService{};
        GAMEPLAY::MotionIntentService motionIntentService{};
        GAMEPLAY::CharacterMotionStateService characterMotionStateService{};
        PHYSICS::KinematicMotionService kinematicMotionService{};
        PHYSICS::PhysicsWorldService physicsWorldService{};
        PHYSICS::PhysicsCollisionGeometryStore collisionGeometryStore{};
        PHYSICS::PhysicsRuntimeStatusService physicsStatusService{};
        PHYSICS::PhysicsProjectSettings physicsSettings{};
        BINDING::RuntimeBindingStore bindingStore{};
        std::shared_ptr<const BINDING::Snapshot>
            materialProjectionSourceSnapshot{};
        std::shared_ptr<const RENDER3D::MATERIAL::
            MaterialExternalValueSnapshot>
            materialExternalValueSnapshot{};
        VFX::VfxPlaybackService vfxPlaybackService{};
        SAND::SandSurfaceQueryService sandSurfaceQueryService{};
        PresentationTransformService presentationTransformService{};
        RuntimeExtensionHost extensionHost{};
        RuntimeFeatureCatalog featureCatalog{};
        RuntimeFeatureInstallReport featureInstallReport{};
        ComponentSystemPolicy componentSystemPolicy{};
        SystemTypeRegistry systemTypeRegistry{};
        SystemScheduler systemScheduler{};
        ComponentRegistry componentRegistry{};
        SceneRuntimeBuilder builder{};
        AnimationStateMachineAssetStore animationStateMachineAssets{};
        bool playActive = false;
        bool inputContextSnapshotValid = false;
        bool editorInputContextWasActive = false;
        bool gameplayInputContextWasActive = false;
        bool editorDocumentSnapshotValid = false;
        bool editorDocumentDirtySnapshot = false;
        bool initialized = false;
    };

} // namespace HIKARI
