#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

namespace HIKARI::EDITOR::VFXOPERATIONS {

inline AssetDatabase* RequireDatabase(const std::function<AssetDatabase*()>& provider,
                                      EditorOperationResult& outResult) {
    AssetDatabase* database = provider ? provider() : nullptr;
    if (database == nullptr) {
        outResult = OperationFailure("NO_PROJECT", "VFX operations require an open project");
    }
    return database;
}

// 開いている VFX 文書を必要とする操作の前置き。失敗時は outResult に理由を入れて nullptr を返す。
inline AssetDatabase* RequireDocument(VfxWorkspaceController& controller,
                                      const std::function<AssetDatabase*()>& provider,
                                      EditorOperationResult& outResult) {
    AssetDatabase* database = RequireDatabase(provider, outResult);
    if (database == nullptr) {
        return nullptr;
    }
    std::string message{};
    if (!controller.Activate(*database, {}, message)) {
        outResult = OperationFailure("VFX_OPEN_FAILED", message);
        return nullptr;
    }
    return database;
}

inline nlohmann::json ContextData(const VfxWorkspaceController& controller, std::string_view targetId = {}) {
    nlohmann::json data{
        {"workspace", "vfx"},
        {"vfxAssetGuid", controller.GetDocument().GetAssetGuid().value},
    };
    if (!targetId.empty()) {
        data["targetId"] = targetId;
    }
    return data;
}

// 未保存の編集を破棄する操作を、confirmDiscard=true の再実行まで保留する。
inline EditorOperationResult PendingDiscard(const VfxWorkspaceController& controller, std::string message) {
    EditorOperationResult result = OperationFailure("PENDING_CONFIRMATION", std::move(message));
    result.data = ContextData(controller);
    result.hints.push_back("Repeat with confirmDiscard=true to continue");
    return result;
}

} // namespace HIKARI::EDITOR::VFXOPERATIONS
