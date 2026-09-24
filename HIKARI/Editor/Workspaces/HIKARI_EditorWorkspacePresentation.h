#pragma once
#include <array>
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"

namespace HIKARI::EDITOR {
struct EditorWorkspacePresentation {
    EditorWorkspaceId id;
    const char* nameKey;
    EditorGlyph icon;
};
inline constexpr std::array kEditorWorkspacePresentations{
    EditorWorkspacePresentation{EditorWorkspaceId::Scene, "menu.workspace.scene", EditorGlyph::Scene},
    EditorWorkspacePresentation{EditorWorkspaceId::Ui, "uix.workspace", EditorGlyph::Grid},
    EditorWorkspacePresentation{EditorWorkspaceId::Text, "uix.textWorkspace", EditorGlyph::List},
    EditorWorkspacePresentation{EditorWorkspaceId::GameplayCamera, "gc.workspace", EditorGlyph::Camera},
    EditorWorkspacePresentation{EditorWorkspaceId::Cinematics, "menu.workspace.cinematics", EditorGlyph::Sequence},
    EditorWorkspacePresentation{EditorWorkspaceId::ModelCollision, "menu.workspace.modelCollision",
                                EditorGlyph::Object},
    EditorWorkspacePresentation{EditorWorkspaceId::ModelMaterial, "menu.workspace.modelMaterial",
                                EditorGlyph::Material},
    EditorWorkspacePresentation{EditorWorkspaceId::AnimationStateMachine, "menu.workspace.animationStateMachine",
                                EditorGlyph::StateMachine},
    EditorWorkspacePresentation{EditorWorkspaceId::RuntimeBinding, "menu.workspace.runtimeBinding",
                                EditorGlyph::Dependency},
    EditorWorkspacePresentation{EditorWorkspaceId::Vfx, "menu.workspace.vfx", EditorGlyph::Vfx},
    EditorWorkspacePresentation{EditorWorkspaceId::Dialogue, "menu.workspace.dialogue", EditorGlyph::List},
    EditorWorkspacePresentation{EditorWorkspaceId::TerrainSurface, "menu.workspace.terrainSurface",
                                EditorGlyph::TerrainRaise}};
} // namespace HIKARI::EDITOR
