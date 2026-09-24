#pragma once

namespace HIKARI::EDITOR {
struct EditorThemePalette;
}

namespace HIKARI::TOOLS::BAKING {
struct LightingBakeStatus;
}

namespace HIKARI::EDITOR::LIGHTING_BAKE_PANEL {

void DrawWorkflowSummary(const TOOLS::BAKING::LightingBakeStatus& status, const EditorThemePalette& palette);

} // namespace HIKARI::EDITOR::LIGHTING_BAKE_PANEL
