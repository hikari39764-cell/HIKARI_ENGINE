#include "Editor/Authoring/HIKARI_SceneObjectTargetResolver.h"

#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::EDITOR {

    SceneObjectTargetResolution ResolveSceneObjectTarget(
        DocumentSceneBase& scene,
        std::optional<SceneObjectId> objectId,
        std::string_view exactName) {

        SceneObjectTargetResolution result{};
        if (objectId.has_value() && objectId->value != 0u) {
            result.runtimeObject = scene.GetWorld().FindObject(*objectId);
            result.documentObject = FindSceneObjectData(
                scene.GetSceneDocument(),
                *objectId);
            if (result.runtimeObject == nullptr ||
                result.documentObject == nullptr) {
                result.runtimeObject = nullptr;
                result.documentObject = nullptr;
                result.code = "OBJECT_NOT_FOUND";
                result.message = "Scene object id was not found: " +
                    std::to_string(objectId->value);
            }
            return result;
        }

        if (exactName.empty()) {
            result.code = "MISSING_TARGET";
            result.message =
                "Specify objectId, or provide an exact object name.";
            return result;
        }

        for (const auto& object : scene.GetWorld().GetObjects()) {
            if (object != nullptr && object->GetName() == exactName) {
                result.candidates.push_back(object->GetDocumentId());
                if (result.runtimeObject == nullptr) {
                    result.runtimeObject = object.get();
                }
            }
        }
        if (result.candidates.empty()) {
            result.runtimeObject = nullptr;
            result.code = "OBJECT_NOT_FOUND";
            result.message = "Scene object name was not found: " +
                std::string(exactName);
            return result;
        }
        if (result.candidates.size() > 1u) {
            result.runtimeObject = nullptr;
            result.code = "AMBIGUOUS_OBJECT_NAME";
            result.message = "Multiple Scene objects have the exact name '" +
                std::string(exactName) +
                "'. Query scene.objects.query and retry with objectId.";
            return result;
        }

        result.documentObject = FindSceneObjectData(
            scene.GetSceneDocument(),
            result.candidates.front());
        if (result.documentObject == nullptr) {
            result.runtimeObject = nullptr;
            result.code = "OBJECT_NOT_IN_DOCUMENT";
            result.message =
                "The resolved runtime object is not in SceneDocument.";
        }
        return result;
    }

} // namespace HIKARI::EDITOR
