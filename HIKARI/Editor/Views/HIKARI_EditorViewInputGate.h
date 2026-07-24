#pragma once

namespace HIKARI::EDITOR {

    struct EditorViewInputBlockState {
        bool pointer = false;
        bool keyboard = false;

        bool Any() const noexcept {
            return pointer || keyboard;
        }
    };

    EditorViewInputBlockState QueryEditorViewInputBlockState() noexcept;

} // namespace HIKARI::EDITOR
