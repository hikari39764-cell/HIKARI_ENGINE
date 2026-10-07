#pragma once
#include <filesystem>
#include <string>
#include "Core/Text/HIKARI_Localization.h"
#include <json.hpp>

namespace HIKARI::EDITOR {
// エディタ個人設定。プロジェクトとドキュメントの履歴には含めない。
struct EditorPreferences {
    TEXT::Language language = TEXT::Language::English;
    float uiScale = 1.0f;
    float sidebarWidth = 340.0f;
    std::string sidebarPage;
    std::string projectParentDirectory;
    bool compact = false;
    bool tooltips = true;
    bool navigationLabels = false;
    bool highContrast = false;
    int logCapacity = 4000;
};
class EditorPreferencesStore {
public:
    explicit EditorPreferencesStore(std::filesystem::path directory);
    const EditorPreferences& Get() const noexcept { return value_; }
    const std::string& Error() const noexcept { return error_; }
    bool Save(const EditorPreferences& preferences);

private:
    std::filesystem::path directory_;
    EditorPreferences value_;
    nlohmann::json source_ = nlohmann::json::object();
    std::string error_;
};
std::filesystem::path EditorUserSettingsDirectory();
const EditorPreferences& GetEditorPreferences();
bool SaveEditorPreferences(const EditorPreferences& preferences);
const std::string& EditorPreferencesError();
} // namespace HIKARI::EDITOR
