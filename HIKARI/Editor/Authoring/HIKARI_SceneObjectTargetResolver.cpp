#include "Editor/Authoring/HIKARI_SceneObjectTargetResolver.h"

#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::EDITOR {

bool ReadOptionalSceneObjectId(const nlohmann::json& parameters, std::optional<SceneObjectId>& outObjectId,
                               EDITOR::EditorOperationResult& outFailure) {
    const auto found = parameters.find("objectId");
    if (found == parameters.end() || found->is_null()) {
        outObjectId.reset();
        return true;
    }
    if ((!found->is_number_unsigned() && !found->is_number_integer()) ||
        (!found->is_number_unsigned() && found->get<int64_t>() <= 0)) {
        outFailure.code = "INVALID_OBJECT_ID";
        outFailure.message = "objectId must be a positive integer.";
        return false;
    }
    const uint64_t value = found->get<uint64_t>();
    if (value == 0u) {
        outFailure.code = "INVALID_OBJECT_ID";
        outFailure.message = "objectId must be a positive integer.";
        return false;
    }
    outObjectId = SceneObjectId{value};
    return true;
}

EDITOR::EditorOperationResult TargetResolutionFailure(const EDITOR::SceneObjectTargetResolution& resolution) {
    EDITOR::EditorOperationResult result{};
    result.code = resolution.code;
    result.message = resolution.message;
    nlohmann::json candidateIds = nlohmann::json::array();
    for (const SceneObjectId candidate : resolution.candidates) {
        candidateIds.push_back(candidate.value);
        result.details.push_back("candidate objectId=" + std::to_string(candidate.value));
    }
    if (!candidateIds.empty()) {
        result.data["candidateObjectIds"] = std::move(candidateIds);
    }
    return result;
}

SceneObjectTargetResolution ResolveSceneObjectTarget(DocumentSceneBase& scene, std::optional<SceneObjectId> objectId,
                                                     std::string_view exactName) {
    SceneObjectTargetResolution result{};
    if (objectId.has_value() && objectId->value != 0u) {
        result.runtimeObject = scene.GetWorld().FindObject(*objectId);
        result.documentObject = FindSceneObjectData(scene.GetSceneDocument(), *objectId);
        if (result.runtimeObject == nullptr || result.documentObject == nullptr) {
            result.runtimeObject = nullptr;
            result.documentObject = nullptr;
            result.code = "OBJECT_NOT_FOUND";
            result.message = "Scene object id was not found: " + std::to_string(objectId->value);
        }
        return result;
    }

    if (exactName.empty()) {
        result.code = "MISSING_TARGET";
        result.message = "Specify objectId, or provide an exact object name.";
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
        result.message = "Scene object name was not found: " + std::string(exactName);
        return result;
    }
    if (result.candidates.size() > 1u) {
        result.runtimeObject = nullptr;
        result.code = "AMBIGUOUS_OBJECT_NAME";
        result.message = "Multiple Scene objects have the exact name '" + std::string(exactName) +
                         "'. Query scene.objects.query and retry with objectId.";
        return result;
    }

    result.documentObject = FindSceneObjectData(scene.GetSceneDocument(), result.candidates.front());
    if (result.documentObject == nullptr) {
        result.runtimeObject = nullptr;
        result.code = "OBJECT_NOT_IN_DOCUMENT";
        result.message = "The resolved runtime object is not in SceneDocument.";
    }
    return result;
}

} // namespace HIKARI::EDITOR
