#pragma once

#include "Editor/Tools/HIKARI_EditorTool.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI::EDITOR {

class EditorToolHost {
public:
    bool Register(EditorToolDescriptor descriptor);

    bool RequestOpen(EditorToolOpenRequest request);
    // 開いていれば閉じ、閉じていれば RequestOpen と同じに開く。ツールが無ければ false。
    bool Toggle(std::string_view toolId);
    bool IsOpen(std::string_view toolId) const;

    void DrawMenuItem(std::string_view toolId);
    void DrawMenuContents();
    void Draw(EditorToolContext& context);

private:
    struct ToolEntry {
        std::string toolId{};
        std::string displayName{};
        std::string menuGroup{};
        bool open = false;
        EditorToolFactoryFn factory = nullptr;
        std::unique_ptr<IEditorTool> instance{};
        std::optional<EditorToolOpenRequest> pendingOpenRequest{};
    };

    ToolEntry* Find(std::string_view toolId);
    const ToolEntry* Find(std::string_view toolId) const;

    std::vector<ToolEntry> tools_{};
};

} // namespace HIKARI::EDITOR
