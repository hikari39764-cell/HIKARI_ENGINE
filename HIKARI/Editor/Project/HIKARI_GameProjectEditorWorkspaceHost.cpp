#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Editor/Project/HIKARI_GameProjectEditorModuleLibrary.h"

namespace HIKARI::EDITOR {
namespace {

    ImU32 ToImGuiColor(
        EDITOR_PROJECT::EditorWorkspaceColor color) noexcept {
        return ImGui::ColorConvertFloat4ToU32(ImVec4{
            std::clamp(color.r, 0.0f, 1.0f),
            std::clamp(color.g, 0.0f, 1.0f),
            std::clamp(color.b, 0.0f, 1.0f),
            std::clamp(color.a, 0.0f, 1.0f),
        });
    }

    std::string MakeStableLabel(
        std::string_view label,
        std::string_view workspaceId,
        std::string_view controlId) {
        std::string result(label);
        result += "###GameProject/";
        result += workspaceId;
        result += '/';
        result += controlId;
        return result;
    }

} // namespace

    class GameProjectEditorWorkspaceUi final
        : public EDITOR_PROJECT::IEditorWorkspaceUi {
    public:
        void BeginFrame(std::string_view workspaceId) {
            workspaceId_.assign(workspaceId);
            panelDepth_ = 0u;
            tableDepth_ = 0u;
            canvasActive_ = false;
            canvasDrawList_ = nullptr;
        }

        void SetWorkspaceId(std::string_view workspaceId) {
            workspaceId_.assign(workspaceId);
        }

        void EndFrame() noexcept {
            if (canvasActive_) {
                EndCanvas();
            }
            while (tableDepth_ > 0u) {
                ImGui::EndTable();
                --tableDepth_;
            }
            while (panelDepth_ > 0u) {
                ImGui::End();
                --panelDepth_;
            }
            workspaceId_.clear();
        }

        bool BeginPanel(
            std::string_view stablePanelId,
            std::string_view displayName,
            bool* open) override {
            const std::string label = MakeStableLabel(
                displayName,
                workspaceId_,
                stablePanelId);
            ++panelDepth_;
            return ImGui::Begin(label.c_str(), open);
        }

        void EndPanel() override {
            if (panelDepth_ == 0u) {
                return;
            }
            if (canvasActive_) {
                EndCanvas();
            }
            while (tableDepth_ > 0u) {
                ImGui::EndTable();
                --tableDepth_;
            }
            ImGui::End();
            --panelDepth_;
        }

        void Text(std::string_view utf8Text) override {
            ImGui::TextUnformatted(
                utf8Text.data(),
                utf8Text.data() + utf8Text.size());
        }

        void Separator() override {
            ImGui::Separator();
        }

        void SameLine() override {
            ImGui::SameLine();
        }

        bool Button(
            std::string_view stableControlId,
            std::string_view label) override {
            const std::string stableLabel = MakeStableLabel(
                label,
                workspaceId_,
                stableControlId);
            return ImGui::Button(stableLabel.c_str());
        }

        bool InputText(
            std::string_view stableControlId,
            std::string_view label,
            char* utf8Buffer,
            size_t bufferCapacity) override {
            if (utf8Buffer == nullptr || bufferCapacity == 0u) {
                return false;
            }
            const std::string stableLabel = MakeStableLabel(
                label,
                workspaceId_,
                stableControlId);
            return ImGui::InputText(
                stableLabel.c_str(),
                utf8Buffer,
                bufferCapacity);
        }

        bool Checkbox(
            std::string_view stableControlId,
            std::string_view label,
            bool* value) override {
            if (value == nullptr) {
                return false;
            }
            const std::string stableLabel = MakeStableLabel(
                label,
                workspaceId_,
                stableControlId);
            return ImGui::Checkbox(stableLabel.c_str(), value);
        }

        bool InputInt(
            std::string_view stableControlId,
            std::string_view label,
            int32_t* value,
            EDITOR_PROJECT::EditorWorkspaceIntInputOptions options) override {
            if (value == nullptr) {
                return false;
            }
            const std::string stableLabel = MakeStableLabel(
                label,
                workspaceId_,
                stableControlId);
            int edited = static_cast<int>(*value);
            const bool changed = ImGui::InputInt(
                stableLabel.c_str(),
                &edited,
                std::max(1, static_cast<int>(options.step)));
            if (changed) {
                if (options.clampToRange != 0u &&
                    options.minimum <= options.maximum) {
                    edited = std::clamp(
                        edited,
                        static_cast<int>(options.minimum),
                        static_cast<int>(options.maximum));
                }
                *value = static_cast<int32_t>(edited);
            }
            return changed;
        }

        bool InputFloat(
            std::string_view stableControlId,
            std::string_view label,
            float* value,
            EDITOR_PROJECT::EditorWorkspaceFloatInputOptions options)
            override {
            if (value == nullptr) {
                return false;
            }
            const std::string stableLabel = MakeStableLabel(
                label,
                workspaceId_,
                stableControlId);
            const bool changed = ImGui::InputFloat(
                stableLabel.c_str(),
                value,
                std::max(0.0f, options.step));
            if (changed && options.clampToRange != 0u &&
                options.minimum <= options.maximum) {
                *value = std::clamp(
                    *value,
                    options.minimum,
                    options.maximum);
            }
            return changed;
        }

        bool InputFloat2(
            std::string_view stableControlId,
            std::string_view label,
            EDITOR_PROJECT::EditorWorkspacePoint* value,
            EDITOR_PROJECT::EditorWorkspaceFloatInputOptions options)
            override {
            if (value == nullptr) {
                return false;
            }
            const std::string stableLabel = MakeStableLabel(
                label,
                workspaceId_,
                stableControlId);
            float values[2]{ value->x, value->y };
            const float speed = std::max(0.001f, options.step);
            const bool hasRange = options.clampToRange != 0u &&
                options.minimum <= options.maximum;
            const float minimum = hasRange
                ? options.minimum
                : 0.0f;
            const float maximum = hasRange
                ? options.maximum
                : 0.0f;
            const bool changed = ImGui::DragFloat2(
                stableLabel.c_str(),
                values,
                speed,
                minimum,
                maximum);
            if (changed) {
                value->x = values[0];
                value->y = values[1];
                if (hasRange) {
                    value->x = std::clamp(
                        value->x,
                        options.minimum,
                        options.maximum);
                    value->y = std::clamp(
                        value->y,
                        options.minimum,
                        options.maximum);
                }
            }
            return changed;
        }

        bool Color4(
            std::string_view stableControlId,
            std::string_view label,
            EDITOR_PROJECT::EditorWorkspaceColor* value) override {
            if (value == nullptr) {
                return false;
            }
            const std::string stableLabel = MakeStableLabel(
                label,
                workspaceId_,
                stableControlId);
            float color[4]{ value->r, value->g, value->b, value->a };
            const bool changed = ImGui::ColorEdit4(
                stableLabel.c_str(),
                color);
            if (changed) {
                value->r = color[0];
                value->g = color[1];
                value->b = color[2];
                value->a = color[3];
            }
            return changed;
        }

        bool Selectable(
            std::string_view stableControlId,
            std::string_view label,
            bool selected) override {
            const std::string stableLabel = MakeStableLabel(
                label,
                workspaceId_,
                stableControlId);
            return ImGui::Selectable(stableLabel.c_str(), selected);
        }

        bool BeginTable(
            std::string_view stableTableId,
            uint32_t columnCount) override {
            if (columnCount == 0u || columnCount > 64u ||
                panelDepth_ == 0u || canvasActive_) {
                return false;
            }
            const std::string stableLabel = MakeStableLabel(
                "Table",
                workspaceId_,
                stableTableId);
            if (!ImGui::BeginTable(
                    stableLabel.c_str(),
                    static_cast<int>(columnCount),
                    ImGuiTableFlags_Borders |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_Resizable)) {
                return false;
            }
            ++tableDepth_;
            return true;
        }

        void TableNextRow() override {
            if (tableDepth_ != 0u) {
                ImGui::TableNextRow();
            }
        }

        bool TableNextColumn() override {
            return tableDepth_ != 0u && ImGui::TableNextColumn();
        }

        void EndTable() override {
            if (tableDepth_ == 0u) {
                return;
            }
            if (canvasActive_) {
                EndCanvas();
            }
            ImGui::EndTable();
            --tableDepth_;
        }

        EDITOR_PROJECT::EditorWorkspaceCanvasInput BeginCanvas(
            std::string_view stableCanvasId,
            float requestedHeight) override {
            if (canvasActive_ || panelDepth_ == 0u) {
                return {};
            }

            const float width = std::max(
                1.0f,
                ImGui::GetContentRegionAvail().x);
            const float height = std::max(
                1.0f,
                requestedHeight > 0.0f ? requestedHeight : 240.0f);
            const std::string label = MakeStableLabel(
                "Canvas",
                workspaceId_,
                stableCanvasId);
            ImGui::InvisibleButton(
                label.c_str(),
                ImVec2(width, height),
                ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight |
                    ImGuiButtonFlags_MouseButtonMiddle);

            canvasMinimum_ = ImGui::GetItemRectMin();
            canvasSize_ = ImGui::GetItemRectSize();
            canvasDrawList_ = ImGui::GetWindowDrawList();
            canvasDrawList_->AddRectFilled(
                canvasMinimum_,
                ImVec2(
                    canvasMinimum_.x + canvasSize_.x,
                    canvasMinimum_.y + canvasSize_.y),
                IM_COL32(24, 27, 32, 255));
            canvasDrawList_->AddRect(
                canvasMinimum_,
                ImVec2(
                    canvasMinimum_.x + canvasSize_.x,
                    canvasMinimum_.y + canvasSize_.y),
                IM_COL32(78, 86, 98, 255));
            canvasDrawList_->PushClipRect(
                canvasMinimum_,
                ImVec2(
                    canvasMinimum_.x + canvasSize_.x,
                    canvasMinimum_.y + canvasSize_.y),
                true);
            canvasActive_ = true;

            const ImGuiIO& io = ImGui::GetIO();
            EDITOR_PROJECT::EditorWorkspaceCanvasInput input{};
            input.size = { canvasSize_.x, canvasSize_.y };
            input.pointer = {
                io.MousePos.x - canvasMinimum_.x,
                io.MousePos.y - canvasMinimum_.y,
            };
            input.hovered = ImGui::IsItemHovered();
            const bool receivesPointer =
                input.hovered || ImGui::IsItemActive();
            const std::array<int, 3u> buttons{ 0, 1, 2 };
            const std::array<uint32_t, 3u> masks{
                static_cast<uint32_t>(
                    EDITOR_PROJECT::EditorWorkspacePointerButton::Primary),
                static_cast<uint32_t>(
                    EDITOR_PROJECT::EditorWorkspacePointerButton::Secondary),
                static_cast<uint32_t>(
                    EDITOR_PROJECT::EditorWorkspacePointerButton::Middle),
            };
            for (size_t index = 0u; index < buttons.size(); ++index) {
                if (receivesPointer &&
                    ImGui::IsMouseDown(buttons[index])) {
                    input.heldButtons |= masks[index];
                }
                if (receivesPointer &&
                    ImGui::IsMouseClicked(buttons[index])) {
                    input.pressedButtons |= masks[index];
                }
                if (receivesPointer &&
                    ImGui::IsMouseReleased(buttons[index])) {
                    input.releasedButtons |= masks[index];
                }
            }
            return input;
        }

        void EndCanvas() override {
            if (!canvasActive_) {
                return;
            }
            canvasDrawList_->PopClipRect();
            canvasActive_ = false;
            canvasDrawList_ = nullptr;
        }

        void CanvasLine(
            EDITOR_PROJECT::EditorWorkspacePoint from,
            EDITOR_PROJECT::EditorWorkspacePoint to,
            EDITOR_PROJECT::EditorWorkspaceColor color,
            float thickness) override {
            if (!canvasActive_) {
                return;
            }
            canvasDrawList_->AddLine(
                ToScreen(from),
                ToScreen(to),
                ToImGuiColor(color),
                std::max(0.5f, thickness));
        }

        void CanvasRect(
            EDITOR_PROJECT::EditorWorkspacePoint minimum,
            EDITOR_PROJECT::EditorWorkspacePoint maximum,
            EDITOR_PROJECT::EditorWorkspaceColor color,
            float thickness) override {
            if (!canvasActive_) {
                return;
            }
            canvasDrawList_->AddRect(
                ToScreen(minimum),
                ToScreen(maximum),
                ToImGuiColor(color),
                0.0f,
                0,
                std::max(0.5f, thickness));
        }

        void CanvasCircle(
            EDITOR_PROJECT::EditorWorkspacePoint center,
            float radius,
            EDITOR_PROJECT::EditorWorkspaceColor color,
            float thickness) override {
            if (!canvasActive_) {
                return;
            }
            canvasDrawList_->AddCircle(
                ToScreen(center),
                std::max(0.0f, radius),
                ToImGuiColor(color),
                0,
                std::max(0.5f, thickness));
        }

        void CanvasText(
            EDITOR_PROJECT::EditorWorkspacePoint position,
            EDITOR_PROJECT::EditorWorkspaceColor color,
            std::string_view utf8Text) override {
            if (!canvasActive_) {
                return;
            }
            canvasDrawList_->AddText(
                ToScreen(position),
                ToImGuiColor(color),
                utf8Text.data(),
                utf8Text.data() + utf8Text.size());
        }

    private:
        ImVec2 ToScreen(
            EDITOR_PROJECT::EditorWorkspacePoint point) const noexcept {
            return {
                canvasMinimum_.x + point.x,
                canvasMinimum_.y + point.y,
            };
        }

        std::string workspaceId_{};
        size_t panelDepth_ = 0u;
        size_t tableDepth_ = 0u;
        bool canvasActive_ = false;
        ImVec2 canvasMinimum_{};
        ImVec2 canvasSize_{};
        ImDrawList* canvasDrawList_ = nullptr;
    };

    GameProjectEditorWorkspaceHost::GameProjectEditorWorkspaceHost()
        : ui_(std::make_unique<GameProjectEditorWorkspaceUi>()) {
    }

    GameProjectEditorWorkspaceHost::~GameProjectEditorWorkspaceHost() =
        default;

    bool GameProjectEditorWorkspaceHost::RegisterModule(
        const GameProjectEditorModuleLibrary& module,
        std::string_view projectRootUtf8,
        std::string& outMessage) {
        if (!module.IsLoaded()) {
            outMessage.clear();
            return true;
        }
        registry_.SetProjectRootUtf8(std::string(projectRootUtf8));
        std::vector<EDITOR_PROJECT::IEditorWorkspace*> workspaces{};
        workspaces.reserve(module.GetWorkspaceCount());
        for (size_t index = 0u;
            index < module.GetWorkspaceCount();
            ++index) {
            workspaces.push_back(module.GetWorkspace(index));
        }
        const bool registered = registry_.RegisterModule(
            module.GetModuleId(),
            workspaces.data(),
            workspaces.size(),
            outMessage);
        if (registered) {
            moduleFailureMessage_.clear();
        }
        return registered;
    }

    void GameProjectEditorWorkspaceHost::ReportModuleFailure(
        std::string message) {
        moduleFailureMessage_ = std::move(message);
    }

    void GameProjectEditorWorkspaceHost::Draw(
        float unscaledDeltaSeconds) {
        if (!IsAvailable()) {
            return;
        }

        ui_->BeginFrame(registry_.GetActiveWorkspaceId());
        registry_.BeginFrame(*ui_, unscaledDeltaSeconds);
        DrawWorkspaceSelector();
        registry_.ProcessPending();
        DrawClosePrompt();
        ui_->SetWorkspaceId(registry_.GetActiveWorkspaceId());
        registry_.DrawActive();
        registry_.EndFrame();
        ui_->EndFrame();
    }

    void GameProjectEditorWorkspaceHost::Shutdown() noexcept {
        if (!ui_) {
            return;
        }
        ui_->BeginFrame(registry_.GetActiveWorkspaceId());
        registry_.Shutdown(*ui_);
        ui_->EndFrame();
        moduleFailureMessage_.clear();
        closePromptWasPending_ = false;
    }

    bool GameProjectEditorWorkspaceHost::IsAvailable() const noexcept {
        return registry_.GetWorkspaceCount() != 0u ||
            !moduleFailureMessage_.empty();
    }

    std::string_view
        GameProjectEditorWorkspaceHost::GetModuleFailure() const noexcept {
        return moduleFailureMessage_;
    }

    GameProjectEditorWorkspaceRegistry&
        GameProjectEditorWorkspaceHost::Registry() noexcept {
        return registry_;
    }

    const GameProjectEditorWorkspaceRegistry&
        GameProjectEditorWorkspaceHost::Registry() const noexcept {
        return registry_;
    }

    void GameProjectEditorWorkspaceHost::DrawWorkspaceSelector() {
        if (!ImGui::Begin(
                "Project Workspaces###GameProject/WorkspaceSelector")) {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(
            "Editor-only workspaces registered by the current project.");
        ImGui::Separator();
        if (!moduleFailureMessage_.empty()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.38f, 0.38f, 1.0f),
                "%.*s",
                static_cast<int>(moduleFailureMessage_.size()),
                moduleFailureMessage_.data());
            if (registry_.GetWorkspaceCount() != 0u) {
                ImGui::Separator();
            }
        }
        for (size_t index = 0u;
            index < registry_.GetWorkspaceCount();
            ++index) {
            const GameProjectEditorWorkspaceInfo info =
                registry_.GetWorkspaceInfo(index);
            ImGui::PushID(static_cast<int>(index));
            if (info.active) {
                ImGui::TextDisabled("Active");
                ImGui::SameLine();
            }
            if (info.disabled) {
                ImGui::BeginDisabled();
            }
            const std::string label(info.displayName);
            if (ImGui::Button(label.c_str())) {
                (void)registry_.RequestActivateWorkspace(info.workspaceId);
            }
            if (info.disabled) {
                ImGui::EndDisabled();
                if (!info.failureMessage.empty() &&
                    ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "%.*s",
                        static_cast<int>(info.failureMessage.size()),
                        info.failureMessage.data());
                }
            }
            ImGui::PopID();
        }

        if (registry_.HasActiveWorkspace()) {
            ImGui::Separator();
            if (ImGui::Button("Save Active")) {
                (void)registry_.RequestSaveActiveWorkspace();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close Active")) {
                (void)registry_.RequestCloseActiveWorkspace();
            }
            if (registry_.IsActiveWorkspaceDirty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("Unsaved changes");
            }
        }

        if (!registry_.GetStatusMessage().empty()) {
            ImGui::Separator();
            ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            if (registry_.GetStatusSeverity() ==
                EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Warning) {
                color = ImVec4(1.0f, 0.78f, 0.30f, 1.0f);
            }
            else if (registry_.GetStatusSeverity() ==
                EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Error) {
                color = ImVec4(1.0f, 0.38f, 0.38f, 1.0f);
            }
            ImGui::TextColored(
                color,
                "%.*s",
                static_cast<int>(
                    registry_.GetStatusMessage().size()),
                registry_.GetStatusMessage().data());
        }
        ImGui::End();
    }

    void GameProjectEditorWorkspaceHost::DrawClosePrompt() {
        const bool pending = registry_.IsAwaitingCloseResolution();
        if (pending && !closePromptWasPending_) {
            ImGui::OpenPopup("Unsaved Project Workspace");
        }
        closePromptWasPending_ = pending;
        if (!pending) {
            return;
        }

        if (ImGui::BeginPopupModal(
                "Unsaved Project Workspace",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::string message =
                "Save changes to '" +
                std::string(registry_.GetAwaitingWorkspaceName()) +
                "' before leaving?";
            ImGui::TextUnformatted(message.c_str());
            ImGui::Separator();
            if (ImGui::Button("Save")) {
                registry_.ResolvePendingClose(
                    GameProjectWorkspaceCloseResolution::Save);
                if (!registry_.IsAwaitingCloseResolution()) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard")) {
                registry_.ResolvePendingClose(
                    GameProjectWorkspaceCloseResolution::Discard);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                registry_.ResolvePendingClose(
                    GameProjectWorkspaceCloseResolution::Cancel);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

} // namespace HIKARI::EDITOR
