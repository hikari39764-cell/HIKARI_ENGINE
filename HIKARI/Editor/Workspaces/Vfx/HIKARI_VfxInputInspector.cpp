#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <array>
#include <string_view>

#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void VfxWorkspaceController::DrawInputInspector(AssetDatabase& database, const VFX::VfxExposedInputData& source) {
#if defined(HIKARI_WITH_EDITOR)
    const VFX::VfxExposedInputData input = source;
    ImGui::TextDisabled(VfxUiText("EXPOSED INPUT").c_str());
    ImGui::TextWrapped(TEXT::Localize("editor.ui.stable.id.s.f22a81").c_str(), input.id.c_str());
    VfxInspectorPropertyEditor editor(*this, database, input.id, statusMessage_);
    editor.Text("Name", "name", input.name);
    constexpr std::array<std::string_view, 7u> types{"Bool", "Int", "Float", "Float2", "Float3", "Float4", "Color"};
    editor.Choice("Type", "type", VFX::ToString(input.type), types.data(), types.size());
    editor.TypedValue("Default", "defaultValue", input.defaultValue);
    if (ImGui::Button(VfxUiText("Delete Input").c_str())) {
        requestDeleteInput_ = true;
    }
#else
    (void)database;
    (void)source;
#endif
}

} // namespace HIKARI::EDITOR
