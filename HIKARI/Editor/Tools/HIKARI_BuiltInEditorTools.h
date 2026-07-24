#pragma once

#include <span>
#include <string_view>

#include "Editor/Tools/HIKARI_EditorTool.h"

namespace HIKARI::EDITOR {

    class EditorToolHost;

    inline constexpr std::string_view kLightingBakeToolId = "LightingBakeTool";

    std::span<const EditorToolDescriptor> GetBuiltInEditorTools();
    void RegisterBuiltInEditorTools(EditorToolHost& host);

} // namespace HIKARI::EDITOR
