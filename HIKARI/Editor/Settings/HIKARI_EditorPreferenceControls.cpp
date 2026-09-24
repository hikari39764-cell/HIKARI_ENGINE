#include "HIKARI_EditorPreferenceControls.h"
#include "imgui.h"
#include <cmath>
#include <string_view>

namespace HIKARI::EDITOR {
bool DrawEditorPreferenceControl(const char* key, EditorPreferences& value) {
    const std::string_view field(key);
    const auto label = TEXT::LocalizeWindow(key, key);
    if (field == "settings.language") {
        ImGui::TextUnformatted(TEXT::LocalizeCStr(key));
        int language = static_cast<int>(value.language);
        ImGui::SetNextItemWidth(-1);
        if (!ImGui::Combo("##language", &language, "English\0中文\0日本語\0"))
            return false;
        value.language = static_cast<TEXT::Language>(language);
        return true;
    }
    if (field == "shell.scale") {
        ImGui::TextUnformatted(TEXT::LocalizeCStr(key));
        constexpr float scales[]{0.85f, 1.0f, 1.15f, 1.3f, 1.5f, 1.75f};
        int choice = 0;
        for (int i = 1; i < 6; ++i)
            if (std::abs(value.uiScale - scales[i]) < std::abs(value.uiScale - scales[choice]))
                choice = i;
        ImGui::SetNextItemWidth(-1);
        if (!ImGui::Combo("##scale", &choice, "85%\0 100%\0 115%\0 130%\0 150%\0 175%\0"))
            return false;
        value.uiScale = scales[choice];
        return true;
    }
    if (field == "shell.tooltips")
        return ImGui::Checkbox(label.c_str(), &value.tooltips);
    if (field == "shell.compact")
        return ImGui::Checkbox(label.c_str(), &value.compact);
    if (field == "shell.highContrast")
        return ImGui::Checkbox(label.c_str(), &value.highContrast);
    if (field == "shell.navigationLabels")
        return ImGui::Checkbox(label.c_str(), &value.navigationLabels);
    return false;
}
} // namespace HIKARI::EDITOR
