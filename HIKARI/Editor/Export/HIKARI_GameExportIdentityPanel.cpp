#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Export/HIKARI_GameExportIdentityPanel.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Platform/HIKARI_EditorFileDialog.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>

namespace HIKARI::EDITOR {
namespace {
template <size_t N>
void Copy(std::array<char, N>& target, const std::string& text) {
    target.fill(0);
    std::memcpy(target.data(), text.data(), (std::min)(text.size(), N - 1));
}
} // namespace
void GameExportIdentityPanel::Reset() {
    root_.clear();
    loaded_ = false;
    dirty_ = false;
    message_.clear();
}
nlohmann::json GameExportIdentityPanel::Draft() const {
    return {{"executableName", executable_.data()}, {"windowTitle", title_.data()}, {"iconPath", icon_.data()}};
}
void GameExportIdentityPanel::SetDocument(const nlohmann::json& document) {
    Copy(executable_, document.value("executableName", std::string("HIKARI_Game.exe")));
    Copy(title_, document.value("windowTitle", std::string{}));
    Copy(icon_, document.value("iconPath", std::string{}));
    saved_ = Draft();
    loaded_ = true;
    dirty_ = false;
}
void GameExportIdentityPanel::Draw(EditorOperationRegistry& operations, bool busy) {
    const auto project = GetProjectContext().GetRoot();
    if (root_ != project) {
        Reset();
        root_ = project;
        const auto result = operations.Execute("export.settings.get");
        if (result.ok)
            SetDocument(result.data);
        else
            message_ = result.message;
    }
    ImGui::SeparatorText(TEXT::Localize("editor.gameInfo").c_str());
    ImGui::BeginDisabled(busy || !loaded_);
    ImGui::InputText(TEXT::LocalizeWindow("editor.executableName", "EXE ファイル名").c_str(), executable_.data(),
                     executable_.size());
    ImGui::InputText(TEXT::LocalizeWindow("editor.windowTitle", "ウィンドウタイトル").c_str(), title_.data(),
                     title_.size());
    ImGui::InputText(TEXT::LocalizeWindow("editor.icon", "アイコン (.ico)").c_str(), icon_.data(), icon_.size());
    if (ImGui::Button(TEXT::LocalizeWindow("editor.chooseIcon", "アイコンを選ぶ...").c_str())) {
        if (const auto selected = FILE_DIALOG::PickFile(project, L"Windows Icon (*.ico)", L"*.ico")) {
            std::error_code error;
            const auto relative = std::filesystem::relative(*selected, project, error);
            const bool inside = !error && !relative.empty() && *relative.begin() != "..";
            Copy(icon_, TEXT::PathToUtf8(inside ? relative : *selected));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.clearIcon", "アイコンを解除").c_str()))
        icon_.fill(0);
    ImGui::TextWrapped(TEXT::Localize("editor.iconHint").c_str());
    dirty_ = loaded_ && Draft() != saved_;
    ImGui::BeginDisabled(!dirty_);
    if (ImGui::Button(TEXT::LocalizeWindow("editor.saveGameInfo", "ゲーム情報を保存").c_str())) {
        const auto result = operations.Execute("export.settings.set", Draft());
        message_ = result.message;
        if (result.ok)
            SetDocument(result.data);
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.revertChanges", "変更を戻す").c_str())) {
        SetDocument(saved_);
        message_.clear();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!loaded_ && ImGui::Button(TEXT::LocalizeWindow("ui.input.reload", "再読み込み").c_str()))
        Reset();
    if (dirty_)
        ImGui::TextUnformatted(TEXT::Localize("editor.saveGameInfoHint").c_str());
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());
    ImGui::Separator();
}
} // namespace HIKARI::EDITOR
