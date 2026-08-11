#pragma once

#include <optional>

#include "Assets/HIKARI_AssetGuid.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"

namespace HIKARI {

    struct AssetWorkspaceActivation;

namespace EDITOR {

    struct AssetWorkspaceActivationRoute {
        std::optional<AssetGuid> sceneAssetGuid{};
        std::optional<EditorWorkspaceOpenRequest> workspaceRequest{};
    };

    AssetWorkspaceActivationRoute RouteAssetWorkspaceActivation(
        const AssetWorkspaceActivation& activation);

} // namespace EDITOR
} // namespace HIKARI
