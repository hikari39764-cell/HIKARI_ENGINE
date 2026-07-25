#include "Scene/Components/HIKARI_AnimationStateMachineComponent.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Components/HIKARI_CharacterInputComponent.h"
#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/HIKARI_SequencePlayerComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

namespace HIKARI {

    void AnimationStateMachineComponent::BuildInspector(
        IInspectorBuilder&) {}
    void AnimationStateMachineComponent::RenderImGui() {}

    void AnimatorComponent::BuildInspector(IInspectorBuilder&) {}
    void AnimatorComponent::RenderImGui() {}

    void CameraActivationVolumeComponent::BuildInspector(
        IInspectorBuilder&) {}
    void CameraComponent::BuildInspector(IInspectorBuilder&) {}
    void CameraFollowComponent::BuildInspector(IInspectorBuilder&) {}
    void CharacterInputComponent::BuildInspector(IInspectorBuilder&) {}
    void CharacterLocomotionComponent::BuildInspector(
        IInspectorBuilder&) {}
    void ColliderComponent::BuildInspector(IInspectorBuilder&) {}
    void PhysicsBodyComponent::BuildInspector(IInspectorBuilder&) {}
    void ProceduralMeshComponent::BuildInspector(IInspectorBuilder&) {}

    void SequencePlayerComponent::BuildInspector(IInspectorBuilder&) {}
    void SequencePlayerComponent::RenderImGui() {}

    void SpawnPointComponent::BuildInspector(IInspectorBuilder&) {}

    void MaterialFxComponent::BuildInspector(IInspectorBuilder&) {}
    void MaterialFxComponent::RenderImGui() {}

    void ModelComponent::BuildInspector(IInspectorBuilder&) {}
    void ModelComponent::RenderImGui() {}

} // namespace HIKARI
