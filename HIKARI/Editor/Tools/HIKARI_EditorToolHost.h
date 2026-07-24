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

        bool OpenTool(std::string_view toolId);
        bool RequestOpen(EditorToolOpenRequest request);
        bool CloseTool(std::string_view toolId);
        bool IsToolOpen(std::string_view toolId) const;

        void DrawMenuItem(std::string_view toolId);
        void DrawMenuContents();
        void Draw(EditorToolContext& context);

        size_t GetRegisteredToolCount() const noexcept;

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
