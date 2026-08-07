#include "Editor/Settings/HIKARI_EditorSettingsPanel.h"

#include "Core/Text/HIKARI_Localization.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void EditorSettingsPanel::Draw(bool& open, EditorContext& context) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(
            TEXT::Localize("settings.title").c_str(), &open)) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText(
        TEXT::Localize("settings.language").c_str());
    std::string languagesBuffer =
        TEXT::Localize("settings.language.english");
    languagesBuffer += '\0';
    languagesBuffer += TEXT::Localize("settings.language.chinese");
    languagesBuffer += '\0';
    languagesBuffer += TEXT::Localize("settings.language.japanese");
    languagesBuffer += '\0';
    languagesBuffer += '\0';
    const char* languages = languagesBuffer.c_str();
    int current = static_cast<int>(TEXT::GetLocalizationLanguage());
    if (ImGui::Combo(
            "##EditorLanguage", &current, languages)) {
        TEXT::SetLocalizationLanguage(
            static_cast<TEXT::Language>(current));
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s",
        TEXT::Localize("settings.product.note").c_str());
    (void)context;
    ImGui::End();
#else
    (void)open;
    (void)context;
#endif
}

} // namespace HIKARI::EDITOR
