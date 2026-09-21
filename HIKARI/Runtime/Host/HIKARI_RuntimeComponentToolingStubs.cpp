#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Assets/Models/Loading/HIKARI_AssimpModelLoader.h"
#include "Assets/Models/Loading/HIKARI_GltfModelLoader.h"
#include "Assets/Models/Loading/HIKARI_ObjModelLoader.h"
#include "Scene/Components/HIKARI_AnimationStateMachineComponent.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Components/HIKARI_CharacterInputComponent.h"
#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"
#include "Scene/Components/HIKARI_DialogueInteractorComponent.h"
#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/HIKARI_SequencePlayerComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"
#include "Scene/Sand/HIKARI_SandSurfaceFollowerComponent.h"

namespace HIKARI {

    void AnimationStateMachineComponent::BuildInspector(
        IInspectorBuilder&) {}
    void AnimationStateMachineComponent::RenderImGui() {}

    void AnimatorComponent::BuildInspector(IInspectorBuilder&) {}
    void AnimatorComponent::RenderImGui() {}

    void CameraActivationVolumeComponent::BuildInspector(
        IInspectorBuilder&) {}
    void CameraComponent::BuildInspector(IInspectorBuilder&) {}
    void CameraFlowComponent::BuildInspector(IInspectorBuilder&) {}
    void CameraFollowComponent::BuildInspector(IInspectorBuilder&) {}
    void CharacterInputComponent::BuildInspector(IInspectorBuilder&) {}
    void CharacterLocomotionComponent::BuildInspector(
        IInspectorBuilder&) {}
    void DialogueInteractorComponent::BuildInspector(
        IInspectorBuilder&) {}
    void DialogueTriggerComponent::BuildInspector(
        IInspectorBuilder&) {}
    void ColliderComponent::BuildInspector(IInspectorBuilder&) {}
    void PhysicsBodyComponent::BuildInspector(IInspectorBuilder&) {}
    void ProceduralMeshComponent::BuildInspector(IInspectorBuilder&) {}
    void IrradianceVolumeComponent::BuildInspector(IInspectorBuilder&) {}
    void LightComponent::BuildInspector(IInspectorBuilder&) {}
    void ReflectionProbeComponent::BuildInspector(IInspectorBuilder&) {}

    void SequencePlayerComponent::BuildInspector(IInspectorBuilder&) {}
    void SequencePlayerComponent::RenderImGui() {}

    void SpawnPointComponent::BuildInspector(IInspectorBuilder&) {}

    void MaterialFxComponent::BuildInspector(IInspectorBuilder&) {}
    void MaterialFxComponent::RenderImGui() {}

    void ModelComponent::BuildInspector(IInspectorBuilder&) {}
    void ModelComponent::RenderImGui() {}

    void TerrainSurfaceComponent::BuildInspector(IInspectorBuilder&) {}
    void WaterSurfaceComponent::BuildInspector(IInspectorBuilder&) {}
    void SandSurfaceFollowerComponent::BuildInspector(
        IInspectorBuilder&) {}

} // namespace HIKARI

namespace HIKARI::ASSETS::MODELS {

    // Source-model import belongs to the Editor authoring product. Runtime
    // and isolated Runtime contract hosts consume cooked model artifacts.
    bool LoadAssimpModelSource(ModelAsset&) { return false; }
    bool LoadGltfModelSource(ModelAsset&) { return false; }
    bool LoadObjModelSource(ModelAsset&) { return false; }

} // namespace HIKARI::ASSETS::MODELS
