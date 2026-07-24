#pragma once

#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    EditorGlyph AssetTypeGlyph(AssetType type) noexcept;

#if defined(HIKARI_WITH_EDITOR)
    ImVec4 AssetTypeIconColor(AssetType type) noexcept;
    void DrawAssetTypeGlyph(
        AssetType type,
        const ImVec2& size = ImVec2(38.0f, 38.0f));
    void DrawFolderGlyph(
        const ImVec2& size = ImVec2(16.0f, 16.0f));
#endif

} // namespace HIKARI::EDITOR
