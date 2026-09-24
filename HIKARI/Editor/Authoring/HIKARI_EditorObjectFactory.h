#pragma once

#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"
#include "Render3D/WorldText/HIKARI_WorldTextFrameSource.h"

namespace HIKARI {

class DocumentSceneBase;
class GameObject;

namespace EDITOR {

struct CreateObjectRequest {
    std::string name{};
    MATH::Vec3 position{0.0f, 0.0f, 0.0f};
    MATH::Vec3 rotation{0.0f, 0.0f, 0.0f};
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct CreatePrimitiveRequest {
    CreateObjectRequest object{};
    ProceduralMeshSettings mesh{};
    bool addCollider = true;
};

struct CreateWorldTextRequest {
    CreateObjectRequest object{};
    std::string text{"World Text"};
    std::string fontAssetPath{"Fonts/NotoSansCJKjp-Regular.otf"};
    MATH::Vec2 layoutSize{4.0f, 1.0f};
    MATH::Vec2 pivot{0.5f, 0.5f};
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    MATH::Vec3 outlineColor{};
    float fontHeight = 0.5f;
    float lineHeightMultiplier = 1.2f;
    float letterSpacing = 0.0f;
    float outlineWidth = 0.04f;
    float opacity = 1.0f;
    float revealAmount = 1.0f;
    int transparentSortPriority = 0;
    TEXT_RENDERING::SdfTextHorizontalAlign horizontalAlign = TEXT_RENDERING::SdfTextHorizontalAlign::Center;
    TEXT_RENDERING::SdfTextVerticalAlign verticalAlign = TEXT_RENDERING::SdfTextVerticalAlign::Center;
    TEXT_RENDERING::SdfTextWrap wrap = TEXT_RENDERING::SdfTextWrap::Character;
    RENDER3D::WORLD_TEXT::WorldTextFacingMode facing = RENDER3D::WORLD_TEXT::WorldTextFacingMode::CameraFacing;
    bool visible = true;
};

GameObject* CreateEmptyObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

GameObject* CreateModelObject(DocumentSceneBase& scene, const AssetGuid& modelGuid, const CreateObjectRequest& request);

GameObject* CreateWorldSpriteObject(DocumentSceneBase& scene, const AssetGuid& textureGuid,
                                    const CreateObjectRequest& request);

GameObject* CreateWorldTextObject(DocumentSceneBase& scene, const CreateWorldTextRequest& request);

GameObject* CreatePrimitiveObject(DocumentSceneBase& scene, const CreatePrimitiveRequest& request);

GameObject* CreateWaterSurfaceObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

GameObject* CreateSandSurfaceObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

GameObject* CreateDialogueTriggerObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

GameObject* CreateUiCanvasObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

// Reuses the first authored Canvas or creates one, then adds a visible
// Button object as its child and rebuilds the runtime World once.
GameObject* CreateUiButtonObject(DocumentSceneBase& scene, const CreateObjectRequest& request,
                                 bool addSceneFlow = false);

GameObject* CreateReflectionProbeObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

GameObject* CreateIrradianceVolumeObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

GameObject* CreateLightObject(DocumentSceneBase& scene, const CreateObjectRequest& request,
                              RENDER3D::LIGHTING::SceneLightType lightType);

} // namespace EDITOR
} // namespace HIKARI
