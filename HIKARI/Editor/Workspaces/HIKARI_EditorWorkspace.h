#pragma once

#include <cstdint>
#include <optional>

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

	// エディタワークスペースの識別子を表す列挙型
    enum class EditorWorkspaceId : uint8_t {
        Scene,
        Cinematics,
        ModelCollision,
        AnimationStateMachine,
    };

	// エディタワークスペースのオープンリクエストを表す構造体
    struct EditorWorkspaceOpenRequest {
        EditorWorkspaceId workspaceId = EditorWorkspaceId::Scene;
        std::optional<SceneObjectId> targetCameraObjectId{};
        std::optional<AssetGuid> sequenceAssetGuid{};
        std::optional<AssetGuid> modelAssetGuid{};
        std::optional<AssetGuid> animationStateMachineAssetGuid{};
    };
    
	// エディタワークスペースのアクティベーション情報を表す構造体
    struct EditorWorkspaceActivation {
        EditorWorkspaceId previous = EditorWorkspaceId::Scene;
        EditorWorkspaceId current = EditorWorkspaceId::Scene;
        std::optional<SceneObjectId> targetCameraObjectId{};
        std::optional<AssetGuid> sequenceAssetGuid{};
        std::optional<AssetGuid> modelAssetGuid{};
        std::optional<AssetGuid> animationStateMachineAssetGuid{};
    };

} // namespace HIKARI::EDITOR
