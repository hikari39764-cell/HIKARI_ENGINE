#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

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
    TerrainSurface,
    Project,
    GameplayCamera,
    Ui,
    Text,
    Count,
};

// 操作レジストリの領域名。UI の翻訳名とは独立した識別子を使う。
constexpr std::string_view EditorWorkspaceOperationId(EditorWorkspaceId id) noexcept {
    switch (id) {
    case EditorWorkspaceId::GameplayCamera: return "gameplay_camera";
    case EditorWorkspaceId::Ui: return "ui";
    case EditorWorkspaceId::Text: return "text";
    case EditorWorkspaceId::Scene: return "scene";
    case EditorWorkspaceId::Cinematics: return "cinematics";
    case EditorWorkspaceId::ModelCollision: return "model_collision";
    case EditorWorkspaceId::ModelMaterial: return "model_material";
    case EditorWorkspaceId::AnimationStateMachine: return "animation_state_machine";
    case EditorWorkspaceId::RuntimeBinding: return "runtime_binding";
    case EditorWorkspaceId::Vfx: return "vfx";
    case EditorWorkspaceId::Dialogue: return "dialogue";
    case EditorWorkspaceId::TerrainSurface: return "terrain_surface";
    case EditorWorkspaceId::Project: return "project";
    default: return "general";
    }
}

// エディタワークスペースのオープンリクエストを表す構造体
struct EditorWorkspaceOpenRequest {
    EditorWorkspaceId workspaceId = EditorWorkspaceId::Scene;
    std::optional<SceneObjectId> targetCameraObjectId{};
    std::optional<AssetGuid> sequenceAssetGuid{};
    std::optional<AssetGuid> cameraProfileAssetGuid{};
    std::optional<AssetGuid> textStyleAssetGuid{};
    std::optional<AssetGuid> uiAnimationAssetGuid{};
    std::optional<AssetGuid> modelAssetGuid{};
    std::optional<AssetGuid> materialAssetGuid{};
    std::optional<AssetGuid> animationStateMachineAssetGuid{};
    std::optional<AssetGuid> vfxAssetGuid{};
    std::optional<AssetGuid> dialogueAssetGuid{};
};

// エディタワークスペースのアクティベーション情報を表す構造体
struct EditorWorkspaceActivation {
    EditorWorkspaceId previous = EditorWorkspaceId::Scene;
    EditorWorkspaceId current = EditorWorkspaceId::Scene;
    std::optional<SceneObjectId> targetCameraObjectId{};
    std::optional<AssetGuid> sequenceAssetGuid{};
    std::optional<AssetGuid> cameraProfileAssetGuid{};
    std::optional<AssetGuid> textStyleAssetGuid{};
    std::optional<AssetGuid> uiAnimationAssetGuid{};
    std::optional<AssetGuid> modelAssetGuid{};
    std::optional<AssetGuid> materialAssetGuid{};
    std::optional<AssetGuid> animationStateMachineAssetGuid{};
    std::optional<AssetGuid> vfxAssetGuid{};
    std::optional<AssetGuid> dialogueAssetGuid{};
};

} // namespace HIKARI::EDITOR
