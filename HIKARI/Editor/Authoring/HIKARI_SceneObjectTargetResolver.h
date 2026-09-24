#pragma once

#include <optional>
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include <string>
#include <string_view>
#include <vector>

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

class DocumentSceneBase;
class GameObject;
struct SceneObjectData;

namespace EDITOR {

struct SceneObjectTargetResolution {
    GameObject* runtimeObject = nullptr;
    SceneObjectData* documentObject = nullptr;
    std::string code{};
    std::string message{};
    std::vector<SceneObjectId> candidates{};

    bool IsResolved() const noexcept { return runtimeObject != nullptr && documentObject != nullptr; }
};

bool ReadOptionalSceneObjectId(const nlohmann::json& parameters, std::optional<SceneObjectId>& outObjectId,
                               EditorOperationResult& outFailure);
EditorOperationResult TargetResolutionFailure(const SceneObjectTargetResolution& resolution);

SceneObjectTargetResolution ResolveSceneObjectTarget(DocumentSceneBase& scene, std::optional<SceneObjectId> objectId,
                                                     std::string_view exactName);

} // namespace EDITOR
} // namespace HIKARI
