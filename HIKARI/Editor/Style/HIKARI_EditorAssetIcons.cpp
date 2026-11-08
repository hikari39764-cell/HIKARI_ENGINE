#include "Editor/Style/HIKARI_EditorAssetIcons.h"

#include <algorithm>

#include "Editor/Style/HIKARI_EditorTheme.h"

namespace HIKARI::EDITOR {

EditorGlyph AssetTypeGlyph(AssetType type) noexcept {
    switch (type) {
    case AssetType::Texture: return EditorGlyph::Texture;
    case AssetType::Model: return EditorGlyph::Model;
    case AssetType::Material:
    case AssetType::ModelMaterialBinding: return EditorGlyph::Material;
    case AssetType::Scene: return EditorGlyph::Scene;
    case AssetType::Sky:
    case AssetType::Environment:
    case AssetType::Weather: return EditorGlyph::Sky;
    case AssetType::Animation: return EditorGlyph::Animation;
    case AssetType::Particle:
    case AssetType::VfxEffect:
    case AssetType::PostEffect:
    case AssetType::PostProfile:
    case AssetType::RenderStyle:
    case AssetType::ColorLut: return EditorGlyph::Vfx;
    case AssetType::LightProfile: return EditorGlyph::Light;
    case AssetType::Sequence: return EditorGlyph::Sequence;
    case AssetType::AnimationStateMachine: return EditorGlyph::StateMachine;
    case AssetType::Font:
    case AssetType::TextStyle: return EditorGlyph::List;
    case AssetType::Dialogue: return EditorGlyph::File;
    case AssetType::Audio: return EditorGlyph::Audio;
    case AssetType::Unknown:
    default: return EditorGlyph::File;
    }
}

ImVec4 AssetTypeIconColor(AssetType type) noexcept {
    switch (type) {
    case AssetType::Texture: return ImVec4(0.32f, 0.76f, 0.92f, 1.0f);
    case AssetType::Model: return ImVec4(0.68f, 0.60f, 0.95f, 1.0f);
    case AssetType::Material:
    case AssetType::ModelMaterialBinding: return ImVec4(0.94f, 0.70f, 0.34f, 1.0f);
    case AssetType::Scene: return ImVec4(0.42f, 0.82f, 0.56f, 1.0f);
    case AssetType::Sky:
    case AssetType::Environment:
    case AssetType::Weather: return ImVec4(0.40f, 0.82f, 0.88f, 1.0f);
    case AssetType::Animation: return ImVec4(0.94f, 0.56f, 0.72f, 1.0f);
    case AssetType::Particle:
    case AssetType::VfxEffect: return ImVec4(0.94f, 0.48f, 0.66f, 1.0f);
    case AssetType::PostEffect:
    case AssetType::PostProfile:
    case AssetType::RenderStyle:
    case AssetType::ColorLut: return ImVec4(0.86f, 0.62f, 0.96f, 1.0f);
    case AssetType::LightProfile: return ImVec4(1.0f, 0.84f, 0.42f, 1.0f);
    case AssetType::Sequence: return ImVec4(0.62f, 0.58f, 0.94f, 1.0f);
    case AssetType::AnimationStateMachine: return ImVec4(0.30f, 0.82f, 0.72f, 1.0f);
    case AssetType::Dialogue: return ImVec4(0.34f, 0.68f, 1.0f, 1.0f);
    case AssetType::Audio: return ImVec4(0.46f, 0.86f, 0.62f, 1.0f);
    case AssetType::Unknown:
    default: return GetEditorThemePalette().textMuted;
    }
}

namespace {
void DrawGlyphAtCursor(EditorGlyph glyph, const ImVec2& size, const ImVec4& color) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    const float glyphSize = (std::max)(8.0f, (std::min)(size.x, size.y) - 6.0f);
    const float thickness = (std::max)(1.5f, glyphSize * 0.038f);
    DrawEditorGlyph(*ImGui::GetWindowDrawList(), glyph, ImVec2(min.x + size.x * 0.5f, min.y + size.y * 0.5f), glyphSize,
                    ImGui::GetColorU32(color), thickness);
}
} // namespace

void DrawAssetTypeGlyph(AssetType type, const ImVec2& size) {
    DrawGlyphAtCursor(AssetTypeGlyph(type), size, AssetTypeIconColor(type));
}

void DrawFolderGlyph(const ImVec2& size) {
    DrawGlyphAtCursor(EditorGlyph::Folder, size, ImVec4(0.92f, 0.68f, 0.28f, 1.0f));
}

} // namespace HIKARI::EDITOR
