#include "Editor/Projects/HIKARI_EditorProjectSession.h"

#include <Windows.h>

#include <filesystem>
#include <utility>
#include <vector>

#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Projects/HIKARI_ProjectProcess.h"
#include "Platform/Paths/HIKARI_ExecutablePath.h"
#include "Project/HIKARI_ProjectContext.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

    void EditorProjectSessionController::DrawMainMenu(
        bool transitionAllowed,
        bool hasUnsavedChanges) {

        if (!ImGui::BeginMenu(
                TEXT::Localize("menu.project").c_str())) {
            return;
        }

        const bool transitionPending =
            pendingTransition_ != EditorProjectTransition::None ||
            exitRequested_;
        const bool descriptorBacked =
            GetProjectContext().IsDescriptorBacked();
        const bool canRebuild =
            transitionAllowed &&
            descriptorBacked &&
            !transitionPending;
        ImGui::BeginDisabled(!canRebuild);
        if (ImGui::MenuItem(
                TEXT::Localize(
                    "menu.project.rebuildAndReload").c_str())) {
            RequestTransition(
                EditorProjectTransition::RebuildAndReload,
                hasUnsavedChanges);
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        const bool canReturn =
            transitionAllowed && !transitionPending;
        ImGui::BeginDisabled(!canReturn);
        if (ImGui::MenuItem(
                TEXT::Localize(
                    "menu.project.returnToHub").c_str())) {
            RequestTransition(
                EditorProjectTransition::ReturnToProjectHub,
                hasUnsavedChanges);
        }
        ImGui::EndDisabled();

        if (!transitionAllowed) {
            ImGui::Separator();
            ImGui::TextDisabled(
                "%s",
                TEXT::Localize(
                    "ui.projectSession.stopPlayFirst").c_str());
        }
        else if (!descriptorBacked) {
            ImGui::Separator();
            ImGui::TextDisabled(
                "%s",
                TEXT::Localize(
                    "ui.projectSession.descriptorRequired").c_str());
        }
        if (!statusMessage_.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped(
                "%s",
                statusMessage_.c_str());
        }

        ImGui::EndMenu();
    }

    void EditorProjectSessionController::
        DrawConfirmationDialog() {

        const std::string title = TEXT::LocalizeWindow(
            "ui.projectSession.confirmTitle",
            "Project Session Transition");
        if (openConfirmationRequested_) {
            ImGui::OpenPopup(title.c_str());
            openConfirmationRequested_ = false;
        }
        if (!ImGui::BeginPopupModal(
                title.c_str(),
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        const bool rebuild =
            pendingTransition_ ==
                EditorProjectTransition::RebuildAndReload;
        ImGui::TextWrapped(
            "%s",
            TEXT::Localize(
                rebuild
                    ? "ui.projectSession.rebuildBody"
                    : "ui.projectSession.returnBody").c_str());
        if (hasUnsavedChanges_) {
            ImGui::Spacing();
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.3f, 1.0f),
                "%s",
                TEXT::Localize(
                    "ui.projectSession.unsavedWarning").c_str());
        }
        ImGui::Spacing();
        ImGui::TextDisabled(
            "%s",
            TEXT::Localize(
                "ui.projectSession.saveFirstHint").c_str());

        if (!statusMessage_.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped(
                "%s",
                statusMessage_.c_str());
        }

        ImGui::Separator();
        const std::string confirmLabel = TEXT::Localize(
            hasUnsavedChanges_
                ? (rebuild
                    ? "ui.projectSession.confirmRebuildDiscard"
                    : "ui.projectSession.confirmReturnDiscard")
                : (rebuild
                    ? "ui.projectSession.confirmRebuild"
                    : "ui.projectSession.confirmReturn"));
        if (ImGui::Button(confirmLabel.c_str())) {
            std::string launchMessage{};
            if (LaunchProjectHub(
                    rebuild,
                    launchMessage)) {
                statusMessage_.clear();
                pendingTransition_ =
                    EditorProjectTransition::None;
                exitRequested_ = true;
                ImGui::CloseCurrentPopup();
            }
            else {
                statusMessage_ = std::move(launchMessage);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(
                TEXT::Localize(
                    "ui.projectSession.cancel").c_str())) {
            pendingTransition_ =
                EditorProjectTransition::None;
            statusMessage_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    bool EditorProjectSessionController::
        ShouldExitHost() const noexcept {
        return exitRequested_;
    }

    void EditorProjectSessionController::RequestTransition(
        EditorProjectTransition transition,
        bool hasUnsavedChanges) {

        if (transition == EditorProjectTransition::None ||
            pendingTransition_ != EditorProjectTransition::None ||
            exitRequested_) {
            return;
        }
        pendingTransition_ = transition;
        hasUnsavedChanges_ = hasUnsavedChanges;
        statusMessage_.clear();
        openConfirmationRequested_ = true;
    }

    bool EditorProjectSessionController::LaunchProjectHub(
        bool rebuildAndReload,
        std::string& outMessage) {

        const std::filesystem::path runtimeRoot =
            PLATFORM::GetExecutableDirectory();
        const std::filesystem::path executable =
            runtimeRoot.parent_path() / "HIKARI_ProjectLauncher.exe";
        std::error_code ec{};
        if (runtimeRoot.empty() ||
            !std::filesystem::is_regular_file(
                executable,
                ec) ||
            ec) {
            outMessage =
                "HIKARI Project Hub is missing: " +
                TEXT::PathToUtf8(executable);
            return false;
        }

        std::vector<std::wstring> arguments{};
        if (rebuildAndReload) {
            const ProjectContext& project =
                GetProjectContext();
            if (!project.IsDescriptorBacked() ||
                project.GetDescriptorPath().empty()) {
                outMessage =
                    "The active project has no Project.hikari descriptor.";
                return false;
            }
            arguments = {
                L"--rebuild-project",
                project.GetDescriptorPath().wstring(),
                L"--wait-for-process-id",
                std::to_wstring(GetCurrentProcessId()),
            };
        }

        return PROJECT_PROCESS::LaunchProcess(
            executable,
            arguments,
            runtimeRoot,
            rebuildAndReload
                ? "Could not start the GameProject rebuild handoff"
                : "Could not return to HIKARI Project Hub",
            outMessage);
    }

} // namespace HIKARI::EDITOR
