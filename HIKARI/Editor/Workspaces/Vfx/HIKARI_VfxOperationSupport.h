#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

namespace HIKARI::EDITOR::VFXOPERATIONS {

inline EditorOperationParameter Parameter(std::string name, EditorOperationParameterType type, bool required,
                                          std::string description) {
    return {
        std::move(name),
        type,
        required,
        std::move(description),
    };
}

inline EditorOperationResult Failure(std::string code, std::string message) {
    EditorOperationResult result{};
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

inline EditorOperationResult Success(std::string message, nlohmann::json data = nlohmann::json::object()) {
    EditorOperationResult result{};
    result.ok = true;
    result.code = "OK";
    result.message = std::move(message);
    result.data = std::move(data);
    return result;
}

inline AssetDatabase* RequireDatabase(const std::function<AssetDatabase*()>& provider,
                                      EditorOperationResult& outResult) {
    AssetDatabase* database = provider ? provider() : nullptr;
    if (database == nullptr) {
        outResult = Failure("NO_PROJECT", "VFX operations require an open project");
    }
    return database;
}

inline bool EnsureDocument(VfxWorkspaceController& controller, AssetDatabase& database,
                           EditorOperationResult& outResult) {
    std::string message{};
    if (!controller.Activate(database, {}, message)) {
        outResult = Failure("VFX_OPEN_FAILED", message);
        return false;
    }
    return true;
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

} // namespace HIKARI::EDITOR::VFXOPERATIONS
