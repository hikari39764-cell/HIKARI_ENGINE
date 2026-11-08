#pragma once

#include <string>

#include "Core/HIKARI_AssetGuid.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"
#include "Render3D/WorldText/HIKARI_WorldTextFrameSource.h"
#include "Scene/Components/Lighting/HIKARI_LightSettings.h"

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

GameObject* CreateWorldSpriteObject(DocumentSceneBase& scene, const AssetGuid& assetGuid,
                                    const CreateObjectRequest& request);

GameObject* CreateWorldTextObject(DocumentSceneBase& scene, const CreateWorldTextRequest& request);

GameObject* CreatePrimitiveObject(DocumentSceneBase& scene, const CreatePrimitiveRequest& request);

GameObject* CreateWaterSurfaceObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

GameObject* CreateTerrainSurfaceObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

GameObject* CreateDialogueTriggerObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

GameObject* CreateReflectionProbeObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

GameObject* CreateIrradianceVolumeObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

// 種類と明るさの単位ごとの既定の灯（スポットは届く距離を 8 m にする）。
LightSettings DefaultLightSettings(RENDER3D::LIGHTING::SceneLightType lightType, LightIntensityUnit unit) noexcept;
GameObject* CreateLightObject(DocumentSceneBase& scene, const CreateObjectRequest& request,
                              const LightSettings& settings);

GameObject* CreateDecalObject(DocumentSceneBase& scene, const CreateObjectRequest& request);

// 機位（場面の摄像机）。布景の機位書签、游戏摄像机の固定機位、镜头演出の镜头が同じ物を使う。
GameObject* CreateCameraObject(DocumentSceneBase& scene, const CreateObjectRequest& request, float verticalFovDegrees);

} // namespace EDITOR
} // namespace HIKARI
