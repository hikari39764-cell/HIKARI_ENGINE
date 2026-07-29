#pragma once

#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"

namespace HIKARI {

    class DocumentSceneBase;
    class GameObject;

namespace EDITOR {

    struct CreateObjectRequest {
        std::string name{};
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 rotation{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    struct CreatePrimitiveRequest {
        CreateObjectRequest object{};
        ProceduralMeshSettings mesh{};
        bool addCollider = true;
    };

    GameObject* CreateEmptyObject(
        DocumentSceneBase& scene,
        const CreateObjectRequest& request);

    GameObject* CreateModelObject(
        DocumentSceneBase& scene,
        const AssetGuid& modelGuid,
        const CreateObjectRequest& request);

    GameObject* CreatePrimitiveObject(
        DocumentSceneBase& scene,
        const CreatePrimitiveRequest& request);

    GameObject* CreateReflectionProbeObject(
        DocumentSceneBase& scene,
        const CreateObjectRequest& request);

    GameObject* CreateIrradianceVolumeObject(
        DocumentSceneBase& scene,
        const CreateObjectRequest& request);

    GameObject* CreateLightObject(
        DocumentSceneBase& scene,
        const CreateObjectRequest& request,
        RENDER3D::LIGHTING::SceneLightType lightType);

} // namespace EDITOR
} // namespace HIKARI
