#pragma once

#include <functional>
#include <string>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

inline bool RequireScene(
    const std::function<DocumentSceneBase*()>& sceneProvider,
    EditorOperationResult& result,
    DocumentSceneBase*& outScene) {
  outScene = sceneProvider();
  if (outScene != nullptr) {
    return true;
  }
  result.code = "NO_SCENE";
  result.message = "当前没有可用的场景上下文。";
  return false;
}

} // namespace HIKARI::EDITOR
