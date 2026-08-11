#include "Editor/Workspaces/HIKARI_AssetWorkspaceActivationRouter.h"

#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserPanel.h"

namespace HIKARI::EDITOR {

    AssetWorkspaceActivationRoute RouteAssetWorkspaceActivation(
        const AssetWorkspaceActivation& activation) {

        AssetWorkspaceActivationRoute route{};
        if (!activation.guid.IsValid()) {
            return route;
        }
        if (activation.type == AssetType::Scene) {
            route.sceneAssetGuid = activation.guid;
            return route;
        }

        EditorWorkspaceOpenRequest request{};
        switch (activation.type) {
        case AssetType::Model:
            request.workspaceId = EditorWorkspaceId::ModelCollision;
            request.modelAssetGuid = activation.guid;
            break;
        case AssetType::Sequence:
            request.workspaceId = EditorWorkspaceId::Cinematics;
            request.sequenceAssetGuid = activation.guid;
            break;
        case AssetType::AnimationStateMachine:
            request.workspaceId = EditorWorkspaceId::AnimationStateMachine;
            request.animationStateMachineAssetGuid = activation.guid;
            break;
        case AssetType::VfxEffect:
            request.workspaceId = EditorWorkspaceId::Vfx;
            request.vfxAssetGuid = activation.guid;
            break;
        default:
            return route;
        }
        route.workspaceRequest = std::move(request);
        return route;
    }

} // namespace HIKARI::EDITOR
