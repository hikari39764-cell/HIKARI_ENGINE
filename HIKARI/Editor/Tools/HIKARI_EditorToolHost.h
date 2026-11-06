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
    // 一つの道具を宿主の開閉とは別の open で描く（新窗口 Play の重ね描き）。opened はこのフレームに開いた時。
    // 実体は Draw と共有するので、出力や履歴は編集器の画面と同じものが続く。
    void DrawDetached(std::string_view toolId, EditorToolContext& context, bool& open, bool opened);

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

    // 実体が無ければ作る。作れなければ閉じて false。
    static bool EnsureInstance(ToolEntry& entry);
    ToolEntry* Find(std::string_view toolId);
    const ToolEntry* Find(std::string_view toolId) const;

    std::vector<ToolEntry> tools_{};
};

} // namespace HIKARI::EDITOR
