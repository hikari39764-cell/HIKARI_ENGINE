#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

inline bool RequireScene(const std::function<DocumentSceneBase*()>& sceneProvider, EditorOperationResult& result,
                         DocumentSceneBase*& outScene) {
    outScene = sceneProvider();
    if (outScene != nullptr) {
        return true;
    }
    result.code = "NO_SCENE";
    result.message = "当前没有可用的场景上下文。";
    return false;
}

// (成功, メッセージ) を返す処理の結果。失敗時は failureCode を付ける。
inline EditorOperationResult OperationResult(bool ok, std::string_view failureCode, std::string message) {
    EditorOperationResult result{};
    result.ok = ok;
    result.code = ok ? "OK" : std::string(failureCode);
    result.message = std::move(message);
    return result;
}

inline EditorOperationResult OperationOk(std::string message) { return OperationResult(true, {}, std::move(message)); }

inline EditorOperationResult OperationFailure(std::string_view code, std::string message) {
    return OperationResult(false, code, std::move(message));
}

} // namespace HIKARI::EDITOR
