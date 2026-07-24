#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include <algorithm>
#include <utility>

#include "Input/Runtime/HIKARI_InputService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void InputActionMapEditor::SetStatus(std::string message, bool isError) {
    statusMessage_ = std::move(message);
    statusIsError_ = isError;
}

void InputActionMapEditor::Draw(INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    INPUT::InputActionMap& map = inputService.EditActionMap();
    selectedAction_ = (std::clamp)(selectedAction_, 0,
        (std::max)(0, static_cast<int>(map.actions.size()) - 1));
    selectedContext_ = (std::clamp)(selectedContext_, 0,
        (std::max)(0, static_cast<int>(map.contexts.size()) - 1));

    DrawToolbar(inputService);
    ImGui::Spacing();
    if (ImGui::BeginTabBar(
            "ProjectInputEditorTabs",
            ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        if (ImGui::BeginTabItem("Actions")) {
            DrawActionsTab(inputService);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Contexts & Bindings")) {
            DrawContextsTab(inputService);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Runtime Monitor")) {
            DrawMonitorTab(inputService);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    DrawActionPopups(inputService);
    DrawContextPopups(inputService);
    DrawBindingPopup(inputService);
    DrawCompositePopup(inputService);
    DrawProjectPopups(inputService);
#else
    (void)inputService;
#endif
}

void InputActionMapEditor::DrawToolbar(INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    if (ImGui::Button("Save", ImVec2(92.0f, 0.0f))) {
        std::string error;
        const bool saved = inputService.SaveProject(&error);
        SetStatus(saved ? "Input project saved." : error, !saved);
        if (saved) dirty_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload", ImVec2(92.0f, 0.0f))) {
        if (dirty_) reloadRequested_ = true;
        else {
            std::string error;
            const bool loaded = inputService.ReloadProject(&error);
            SetStatus(loaded ? "Input project reloaded." : error, !loaded);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore Defaults", ImVec2(142.0f, 0.0f))) {
        restoreRequested_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%zu actions  |  %zu contexts",
        inputService.GetActionMap().actions.size(),
        inputService.GetActionMap().contexts.size());
    if (!statusMessage_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(
            statusIsError_
                ? ImVec4(1.0f, 0.4f, 0.32f, 1.0f)
                : ImVec4(0.42f, 0.9f, 0.5f, 1.0f),
            "%s", statusMessage_.c_str());
    }
    ImGui::TextDisabled(
        "%s", inputService.GetInputDirectory().generic_string().c_str());
#else
    (void)inputService;
#endif
}

void InputActionMapEditor::DrawProjectPopups(
    INPUT::InputService& inputService) {
#if defined(HIKARI_WITH_EDITOR)
    if (reloadRequested_) {
        ImGui::OpenPopup("Reload Input Project");
        reloadRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            "Reload Input Project", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Discard unsaved in-memory changes and reload from disk?");
        if (ImGui::Button("Reload", ImVec2(120.0f, 0.0f))) {
            std::string error;
            const bool loaded = inputService.ReloadProject(&error);
            SetStatus(loaded ? "Input project reloaded." : error, !loaded);
            if (loaded) {
                dirty_ = false;
                selectedAction_ = selectedContext_ = 0;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (restoreRequested_) {
        ImGui::OpenPopup("Restore Input Defaults");
        restoreRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            "Restore Input Defaults", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Replace every action, context, and binding with HIKARI defaults?");
        if (ImGui::Button("Restore", ImVec2(120.0f, 0.0f))) {
            std::string error;
            const bool restored = inputService.RestoreDefaults(&error);
            SetStatus(
                restored ? "Default input project restored and saved." : error,
                !restored);
            if (restored) {
                dirty_ = false;
                selectedAction_ = selectedContext_ = 0;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)inputService;
#endif
}

} // namespace HIKARI
