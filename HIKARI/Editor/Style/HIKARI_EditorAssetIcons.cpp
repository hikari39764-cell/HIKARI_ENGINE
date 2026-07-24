#include "Editor/Style/HIKARI_EditorAssetIcons.h"

#if defined(HIKARI_WITH_EDITOR)
#include <algorithm>

#include "Editor/Style/HIKARI_EditorTheme.h"
#endif

namespace HIKARI::EDITOR {

    EditorGlyph AssetTypeGlyph(AssetType type) noexcept {
        switch (type) {
        case AssetType::Texture: return EditorGlyph::Texture;
        case AssetType::Model: return EditorGlyph::Model;
        case AssetType::Material: return EditorGlyph::Material;
        case AssetType::Scene: return EditorGlyph::Scene;
        case AssetType::Sky: return EditorGlyph::Sky;
        case AssetType::Animation: return EditorGlyph::Animation;
        case AssetType::Particle:
        case AssetType::VfxEffect:
            return EditorGlyph::Vfx;
        case AssetType::Sequence: return EditorGlyph::Sequence;
        case AssetType::AnimationStateMachine:
            return EditorGlyph::StateMachine;
        case AssetType::Unknown:
        default:
            return EditorGlyph::File;
        }
    }

#if defined(HIKARI_WITH_EDITOR)
    ImVec4 AssetTypeIconColor(AssetType type) noexcept {
        switch (type) {
        case AssetType::Texture: return ImVec4(0.32f, 0.76f, 0.92f, 1.0f);
        case AssetType::Model: return ImVec4(0.68f, 0.60f, 0.95f, 1.0f);
        case AssetType::Material: return ImVec4(0.94f, 0.70f, 0.34f, 1.0f);
        case AssetType::Scene: return ImVec4(0.42f, 0.82f, 0.56f, 1.0f);
        case AssetType::Sky: return ImVec4(0.40f, 0.82f, 0.88f, 1.0f);
        case AssetType::Animation: return ImVec4(0.94f, 0.56f, 0.72f, 1.0f);
        case AssetType::Particle:
        case AssetType::VfxEffect:
            return ImVec4(0.94f, 0.48f, 0.66f, 1.0f);
        case AssetType::Sequence: return ImVec4(0.62f, 0.58f, 0.94f, 1.0f);
        case AssetType::AnimationStateMachine:
            return ImVec4(0.30f, 0.82f, 0.72f, 1.0f);
        case AssetType::Unknown:
        default:
            return GetEditorThemePalette().textMuted;
        }
    }

    namespace {
        void DrawGlyphAtCursor(
            EditorGlyph glyph,
            const ImVec2& size,
            const ImVec4& color) {
            const ImVec2 min = ImGui::GetCursorScreenPos();
            ImGui::Dummy(size);
            const float glyphSize = (std::max)(
                8.0f,
                (std::min)(size.x, size.y) - 6.0f);
            const float thickness = (std::max)(1.5f, glyphSize * 0.038f);
            DrawEditorGlyph(
                *ImGui::GetWindowDrawList(),
                glyph,
                ImVec2(min.x + size.x * 0.5f, min.y + size.y * 0.5f),
                glyphSize,
                ImGui::GetColorU32(color),
                thickness);
        }
    }

    void DrawAssetTypeGlyph(AssetType type, const ImVec2& size) {
        DrawGlyphAtCursor(
            AssetTypeGlyph(type),
            size,
            AssetTypeIconColor(type));
    }

    void DrawFolderGlyph(const ImVec2& size) {
        DrawGlyphAtCursor(
            EditorGlyph::Folder,
            size,
            ImVec4(0.92f, 0.68f, 0.28f, 1.0f));
    }
#endif

} // namespace HIKARI::EDITOR
