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
        ModelMaterial,
        AnimationStateMachine,
        RuntimeBinding,
        Vfx,
        Dialogue,
        CombatAction,
        TerrainSurface,
        Count,
    };

	// エディタワークスペースのオープンリクエストを表す構造体
    struct EditorWorkspaceOpenRequest {
        EditorWorkspaceId workspaceId = EditorWorkspaceId::Scene;
        std::optional<SceneObjectId> targetCameraObjectId{};
        std::optional<AssetGuid> sequenceAssetGuid{};
        std::optional<AssetGuid> modelAssetGuid{};
        std::optional<AssetGuid> materialAssetGuid{};
        std::optional<AssetGuid> animationStateMachineAssetGuid{};
        std::optional<AssetGuid> vfxAssetGuid{};
        std::optional<AssetGuid> dialogueAssetGuid{};
        std::optional<AssetGuid> combatActionAssetGuid{};
    };
    
	// エディタワークスペースのアクティベーション情報を表す構造体
    struct EditorWorkspaceActivation {
        EditorWorkspaceId previous = EditorWorkspaceId::Scene;
        EditorWorkspaceId current = EditorWorkspaceId::Scene;
        std::optional<SceneObjectId> targetCameraObjectId{};
        std::optional<AssetGuid> sequenceAssetGuid{};
        std::optional<AssetGuid> modelAssetGuid{};
        std::optional<AssetGuid> materialAssetGuid{};
        std::optional<AssetGuid> animationStateMachineAssetGuid{};
        std::optional<AssetGuid> vfxAssetGuid{};
        std::optional<AssetGuid> dialogueAssetGuid{};
        std::optional<AssetGuid> combatActionAssetGuid{};
    };

} // namespace HIKARI::EDITOR
