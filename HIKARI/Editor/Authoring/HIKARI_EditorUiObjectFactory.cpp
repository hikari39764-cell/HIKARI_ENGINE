#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/Authoring/HIKARI_UiAuthoring.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::EDITOR {
namespace {
GameObject* Create(DocumentSceneBase& scene, const CreateObjectRequest& request, std::string_view preset) {
    const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
    const auto result = CreateUiTemplate(scene.GetSceneDocument(), scene.GetComponentRegistry(), preset, {},
                                         FindUiFont(scene.GetAssetDatabase()), request.name);
    if (!result.ok)
        return nullptr;
    for (auto& object : scene.GetSceneDocument().objects)
        if (object.id == result.id) {
            object.transform.position = request.position;
            object.transform.rotationEulerDeg = request.rotation;
            object.transform.scale = request.scale;
            break;
        }
    (void)scene.ApplyRuntimeObjectChanges(DiffSceneObjects(beforeObjects, scene.GetSceneDocument().objects));
    return scene.GetWorld().FindObject(result.id);
}
} // namespace
GameObject* CreateUiCanvasObject(DocumentSceneBase& scene, const CreateObjectRequest& request) {
    return Create(scene, request, "canvas");
}
GameObject* CreateUiButtonObject(DocumentSceneBase& scene, const CreateObjectRequest& request, bool addSceneFlow) {
    return Create(scene, request, addSceneFlow ? "scene_button" : "button");
}
} // namespace HIKARI::EDITOR
