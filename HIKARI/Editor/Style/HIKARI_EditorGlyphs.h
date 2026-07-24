#pragma once

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    enum class EditorGlyph {
        Add,
        Close,
        More,
        Play,
        Pause,
        Stop,
        Save,
        SaveAs,
        Undo,
        Redo,
        Revert,
        Refresh,
        Import,
        Reimport,
        Dependency,
        Inspector,
        Settings,
        Filter,
        Grid,
        List,
        Compact,
        Folder,
        Reveal,
        Delete,
        Keyframe,
        Transform,
        Translate,
        Rotate,
        Scale,
        WorldSpace,
        LocalSpace,
        Light,
        Gizmo,
        Lens,
        Camera,
        Snap,
        Log,
        Library,
        NewDocument,
        Duplicate,
        Focus,
        Lock,
        Object,
        Exit,
        File,
        Texture,
        Model,
        Material,
        Scene,
        Sky,
        Vfx,
        Sequence,
        Animation,
        StateMachine,
        Audio,
    };

#if defined(HIKARI_WITH_EDITOR)
    void DrawEditorGlyph(
        ImDrawList& drawList,
        EditorGlyph glyph,
        const ImVec2& center,
        float size,
        ImU32 color,
        float thickness = 1.6f);
#endif

} // namespace HIKARI::EDITOR
