#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Platform/HIKARI_EditorFileDialog.h"
#include "imgui.h"

namespace HIKARI::EDITOR {
bool VfxWorkspaceController::SetContentLibraryRoot(AssetDatabase& database, const std::filesystem::path& root,
                                                   std::string& message) {
    std::error_code error;
    if (root.empty() || !std::filesystem::is_directory(root, error)) {
        message = "Select an existing shared library folder";
        return false;
    }
    const auto absolute = std::filesystem::absolute(root, error).lexically_normal();
    if (error) {
        message = error.message();
        return false;
    }
    const auto settings = database.GetLibraryRoot() / "Editor/VfxContentLibrary.json";
    if (!SERIALIZATION::JSON::WriteJsonFile(settings, {{"root", TEXT::PathToUtf8(absolute)}}, &message))
        return false;
    contentLibraryRoot_ = absolute;
    libraryProjectRoot_ = database.GetProjectRoot();
    libraryPackages_ = ASSETS::LIBRARY::ScanContentLibrary(contentLibraryRoot_, message);
    if (message.empty())
        message = "Loaded " + std::to_string(libraryPackages_.size()) + " content packages";
    return true;
}

bool VfxWorkspaceController::ImportLibraryPackage(AssetDatabase& database, const std::filesystem::path& path,
                                                  ASSETS::LIBRARY::ContentImportResult& result, std::string& message) {
    if (!ASSETS::LIBRARY::ImportContentPackage(database, path, result, message))
        return false;
    if (!result.reused) {
        if (database.QueueImportAssets(result.assets, "Shared content"))
            message += "; cook queued (see asset import status)";
        else
            message += "; cook could not be queued, use Reimport in Assets";
    }
    return true;
}

bool VfxWorkspaceController::PublishToLibrary(AssetDatabase& database, const ASSETS::LIBRARY::ContentPackageInfo& info,
                                              std::string& message) {
    if (contentLibraryRoot_.empty()) {
        message = "Select a shared library folder first";
        return false;
    }
    if (!document_.IsOpen()) {
        message = "Open an effect before publishing";
        return false;
    }
    if (!Save(database, message))
        return false;
    const std::array roots{document_.GetAssetGuid()};
    if (!ASSETS::LIBRARY::PublishContentPackage(database, roots, contentLibraryRoot_, info, message))
        return false;
    std::string scanMessage;
    libraryPackages_ = ASSETS::LIBRARY::ScanContentLibrary(contentLibraryRoot_, scanMessage);
    return true;
}

void VfxWorkspaceController::DrawContentLibrary(AssetDatabase& database) {
    if (libraryProjectRoot_ != database.GetProjectRoot()) {
        libraryProjectRoot_ = database.GetProjectRoot();
        libraryPackages_.clear();
        contentLibraryRoot_.clear();
        nlohmann::json settings;
        const auto path = database.GetLibraryRoot() / "Editor/VfxContentLibrary.json";
        if (std::filesystem::exists(path) && SERIALIZATION::JSON::ReadJsonFile(path, settings) &&
            settings.contains("root") && settings["root"].is_string()) {
            contentLibraryRoot_ = TEXT::PathFromUtf8(settings["root"].get<std::string>());
            libraryPackages_ = ASSETS::LIBRARY::ScanContentLibrary(contentLibraryRoot_, statusMessage_);
        }
    }
    if (ImGui::Button(VfxUiText("Choose library folder").c_str())) {
        if (auto folder = FILE_DIALOG::PickFolder(contentLibraryRoot_))
            (void)SetContentLibraryRoot(database, *folder, statusMessage_);
    }
    ImGui::SameLine();
    if (ImGui::Button(VfxUiText("Refresh").c_str()) && !contentLibraryRoot_.empty())
        libraryPackages_ = ASSETS::LIBRARY::ScanContentLibrary(contentLibraryRoot_, statusMessage_);
    if (contentLibraryRoot_.empty()) {
        ImGui::TextWrapped(
            VfxUiText("Choose a team library folder. Importing creates an editable project copy with its dependencies.")
                .c_str());
        return;
    }
    ImGui::TextWrapped("%s", TEXT::PathToUtf8(contentLibraryRoot_).c_str());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##SharedSearch", TEXT::Localize("editor.ui.search.effects.and.categories.6fa83b").c_str(),
                             librarySearch_.data(), librarySearch_.size());
    const ImGuiTextFilter filter(librarySearch_.data());
    for (const auto& package : libraryPackages_) {
        if (!filter.PassFilter((package.name + " " + package.category + " " + package.description).c_str()))
            continue;
        ImGui::PushID(TEXT::PathToUtf8(package.manifestPath).c_str());
        ImGui::SeparatorText(package.name.c_str());
        ImGui::TextDisabled(TEXT::Localize("editor.ui.s.v.s.zu.assets.21539e").c_str(), package.category.c_str(),
                            package.version.c_str(), package.assetCount);
        ImGui::TextWrapped("%s", package.description.c_str());
        if (ImGui::Button(VfxUiText("Import into project").c_str())) {
            ASSETS::LIBRARY::ContentImportResult imported;
            (void)ImportLibraryPackage(database, package.manifestPath, imported, statusMessage_);
        }
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader(VfxUiText("Share current effect").c_str())) {
        ImGui::InputText(TEXT::LocalizeWindow("editor.ui.package.id.7db680", "Package ID").c_str(),
                         libraryPackageId_.data(), libraryPackageId_.size());
        ImGui::InputText(TEXT::LocalizeWindow("editor.ui.revision.3beb9a", "Revision").c_str(),
                         libraryPackageRevision_.data(), libraryPackageRevision_.size());
        ImGui::InputText(TEXT::LocalizeWindow("editor.ui.category.a3c686", "Category").c_str(), libraryCategory_.data(),
                         libraryCategory_.size());
        ImGui::InputText(TEXT::LocalizeWindow("editor.ui.credits.license.402861", "Credits / license").c_str(),
                         libraryLicense_.data(), libraryLicense_.size());
        ImGui::TextWrapped(
            VfxUiText(
                "Saves this effect and publishes its source dependencies. Existing revisions are kept; choose a new revision to update.")
                .c_str());
        if (ImGui::Button(VfxUiText("Save and publish").c_str())) {
            ASSETS::LIBRARY::ContentPackageInfo info;
            info.id = libraryPackageId_.data();
            info.version = libraryPackageRevision_.data();
            info.name = document_.Asset().name;
            info.category = libraryCategory_.data();
            info.license = libraryLicense_.data();
            (void)PublishToLibrary(database, info, statusMessage_);
        }
    }
}
} // namespace HIKARI::EDITOR
