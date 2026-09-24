#include "Editor/Settings/HIKARI_EditorPreferences.h"
#include "Core/HIKARI_Logger.h"
#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace HIKARI::EDITOR {
namespace {
EditorPreferences Normalize(EditorPreferences value) {
    if (static_cast<unsigned>(value.language) >= 3)
        value.language = TEXT::Language::English;
    value.uiScale = std::isfinite(value.uiScale) ? std::clamp(value.uiScale, 0.8f, 1.75f) : 1.0f;
    value.sidebarWidth = std::isfinite(value.sidebarWidth) ? std::clamp(value.sidebarWidth, 260.0f, 680.0f) : 340.0f;
    value.logCapacity = std::clamp(value.logCapacity, 200, 20000);
    return value;
}
void Apply(const EditorPreferences& value) {
    TEXT::SetLocalizationLanguage(value.language);
    CORE::SetLogHistoryCapacity(static_cast<size_t>(value.logCapacity));
}
EditorPreferencesStore& Store() {
    static EditorPreferencesStore store(EditorUserSettingsDirectory());
    static const bool applied = [&] {
        Apply(store.Get());
        return true;
    }();
    (void)applied;
    return store;
}
} // namespace

EditorPreferencesStore::EditorPreferencesStore(std::filesystem::path directory) : directory_(std::move(directory)) {
    if (directory_.empty())
        return;
    try {
        std::ifstream input(directory_ / "preferences.json");
        if (input) {
            input >> source_;
            const auto language = source_.value("language", std::string("en"));
            value_.language = language == "zh"   ? TEXT::Language::Chinese
                              : language == "ja" ? TEXT::Language::Japanese
                                                 : TEXT::Language::English;
            value_.uiScale = source_.value("uiScale", 1.0f);
            value_.sidebarWidth = source_.value("sidebarWidth", 340.0f);
            value_.sidebarPage = source_.value("sidebarPage", std::string{});
            value_.projectParentDirectory = source_.value("projectParentDirectory", std::string{});
            value_.compact = source_.value("compact", false);
            value_.tooltips = source_.value("tooltips", true);
            value_.navigationLabels = source_.value("navigationLabels", false);
            value_.highContrast = source_.value("highContrast", false);
            value_.logCapacity = source_.value("logCapacity", 4000);
        }
    } catch (const std::exception& e) {
        error_ = e.what();
        value_ = {};
        source_ = nlohmann::json::object();
        HIKARI_LOG_WARN("[EditorPreferences] Cannot read preferences; using defaults.");
    }
    value_ = Normalize(value_);
}

std::filesystem::path EditorUserSettingsDirectory() {
    PWSTR directory = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &directory)))
        return {};
    const auto result = std::filesystem::path(directory) / "HIKARI" / "Editor";
    CoTaskMemFree(directory);
    return result;
}
const EditorPreferences& GetEditorPreferences() { return Store().Get(); }
const std::string& EditorPreferencesError() { return Store().Error(); }
bool SaveEditorPreferences(const EditorPreferences& preferences) {
    if (!Store().Save(preferences))
        return false;
    Apply(Store().Get());
    return true;
}

bool EditorPreferencesStore::Save(const EditorPreferences& preferences) {
    const auto candidate = Normalize(preferences);
    std::filesystem::path temporary;
    try {
        if (directory_.empty())
            throw std::runtime_error("User settings directory unavailable");
        std::filesystem::create_directories(directory_);
        auto data = source_;
        data["version"] = 1;
        data["language"] = candidate.language == TEXT::Language::Chinese    ? "zh"
                           : candidate.language == TEXT::Language::Japanese ? "ja"
                                                                            : "en";
        data["uiScale"] = candidate.uiScale;
        data["sidebarWidth"] = candidate.sidebarWidth;
        data["sidebarPage"] = candidate.sidebarPage;
        data["projectParentDirectory"] = candidate.projectParentDirectory;
        data["compact"] = candidate.compact;
        data["tooltips"] = candidate.tooltips;
        data["navigationLabels"] = candidate.navigationLabels;
        data["highContrast"] = candidate.highContrast;
        data["logCapacity"] = candidate.logCapacity;
        const auto target = directory_ / "preferences.json";
        temporary = target;
        temporary += L".tmp." + std::to_wstring(GetCurrentProcessId());
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << data.dump(2) << '\n';
            output.flush();
            if (!output)
                throw std::runtime_error("Cannot write editor preferences");
        }
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace editor preferences");
        value_ = candidate;
        source_ = std::move(data);
        error_.clear();
        return true;
    } catch (const std::exception& e) {
        error_ = e.what();
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        HIKARI_LOG_ERROR("[EditorPreferences] Cannot save editor preferences.");
        return false;
    }
}
} // namespace HIKARI::EDITOR
