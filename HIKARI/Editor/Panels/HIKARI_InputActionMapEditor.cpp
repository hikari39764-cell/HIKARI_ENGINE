#include "Editor/Panels/HIKARI_InputActionMapEditor.h"

#include "Core/Text/HIKARI_Localization.h"
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
        if (ImGui::BeginTabItem(
                TEXT::Localize("ui.input.tabActions").c_str())) {
            DrawActionsTab(inputService);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(
                TEXT::Localize(
                    "ui.input.tabContextsBindings").c_str())) {
            DrawContextsTab(inputService);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(
                TEXT::Localize(
                    "ui.input.tabRuntimeMonitor").c_str())) {
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
    if (ImGui::Button(
            TEXT::Localize("ui.input.save").c_str(),
            ImVec2(92.0f, 0.0f))) {
        std::string error;
        const bool saved = inputService.SaveProject(&error);
        SetStatus(
            saved
                ? TEXT::Localize("ui.input.projectSaved")
                : error,
            !saved);
        if (saved) dirty_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(
            TEXT::Localize("ui.input.reload").c_str(),
            ImVec2(92.0f, 0.0f))) {
        if (dirty_) reloadRequested_ = true;
        else {
            std::string error;
            const bool loaded = inputService.ReloadProject(&error);
            SetStatus(
                loaded
                    ? TEXT::Localize("ui.input.projectReloaded")
                    : error,
                !loaded);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(
            TEXT::Localize("ui.input.restoreDefaults").c_str(),
            ImVec2(142.0f, 0.0f))) {
        restoreRequested_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        TEXT::Localize("ui.input.actionsContextsFormat").c_str(),
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
        ImGui::OpenPopup(
            (TEXT::Localize("ui.input.reloadTitle") +
                "###ReloadInputProject").c_str());
        reloadRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            (TEXT::Localize("ui.input.reloadTitle") +
                "###ReloadInputProject").c_str(),
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "%s",
            TEXT::Localize("ui.input.reloadBody").c_str());
        if (ImGui::Button(
                TEXT::Localize("ui.input.reload").c_str(),
                ImVec2(120.0f, 0.0f))) {
            std::string error;
            const bool loaded = inputService.ReloadProject(&error);
            SetStatus(
                loaded
                    ? TEXT::Localize("ui.input.projectReloaded")
                    : error,
                !loaded);
            if (loaded) {
                dirty_ = false;
                selectedAction_ = selectedContext_ = 0;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(
                TEXT::Localize("ui.common.cancel").c_str(),
                ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (restoreRequested_) {
        ImGui::OpenPopup(
            (TEXT::Localize("ui.input.restoreTitle") +
                "###RestoreInputDefaults").c_str());
        restoreRequested_ = false;
    }
    if (ImGui::BeginPopupModal(
            (TEXT::Localize("ui.input.restoreTitle") +
                "###RestoreInputDefaults").c_str(),
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "%s",
            TEXT::Localize("ui.input.restoreBody").c_str());
        if (ImGui::Button(
                TEXT::Localize("ui.input.restore").c_str(),
                ImVec2(120.0f, 0.0f))) {
            std::string error;
            const bool restored = inputService.RestoreDefaults(&error);
            SetStatus(
                restored
                    ? TEXT::Localize("ui.input.defaultRestored")
                    : error,
                !restored);
            if (restored) {
                dirty_ = false;
                selectedAction_ = selectedContext_ = 0;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(
                TEXT::Localize("ui.common.cancel").c_str(),
                ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)inputService;
#endif
}

} // namespace HIKARI
