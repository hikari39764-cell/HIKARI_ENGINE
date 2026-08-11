#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        const char* SeverityLabel(
            VFX::VfxDiagnosticSeverity severity) noexcept {

            switch (severity) {
            case VFX::VfxDiagnosticSeverity::Info: return "Info";
            case VFX::VfxDiagnosticSeverity::Warning: return "Warning";
            case VFX::VfxDiagnosticSeverity::Error: return "Error";
            }
            return "Unknown";
        }

        ImVec4 SeverityColor(
            VFX::VfxDiagnosticSeverity severity) noexcept {

            switch (severity) {
            case VFX::VfxDiagnosticSeverity::Info:
                return { 0.48f, 0.72f, 1.0f, 1.0f };
            case VFX::VfxDiagnosticSeverity::Warning:
                return { 1.0f, 0.72f, 0.22f, 1.0f };
            case VFX::VfxDiagnosticSeverity::Error:
                return { 1.0f, 0.34f, 0.30f, 1.0f };
            }
            return { 1.0f, 1.0f, 1.0f, 1.0f };
        }
#endif
    } // namespace

    void VfxWorkspaceController::DrawDiagnostics() {
#if defined(HIKARI_WITH_EDITOR)
        if (diagnostics_.empty()) {
            ImGui::TextColored(
                { 0.42f, 0.88f, 0.55f, 1.0f },
                "The current VFX source compiles without diagnostics.");
            return;
        }
        if (!ImGui::BeginTable(
                "VfxDiagnosticsTable",
                3,
                ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_ScrollY)) {
            return;
        }
        ImGui::TableSetupColumn(
            "Severity", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn(
            "Code", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn(
            "Message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (size_t index = 0u; index < diagnostics_.size(); ++index) {
            const VFX::VfxDiagnostic& diagnostic = diagnostics_[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(
                SeverityColor(diagnostic.severity),
                "%s",
                SeverityLabel(diagnostic.severity));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(diagnostic.code.c_str());
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Selectable(
                    diagnostic.message.c_str(),
                    false,
                    ImGuiSelectableFlags_SpanAllColumns) &&
                !diagnostic.subjectId.empty()) {
                if (const VFX::VfxEmitterData* emitter =
                        FindEmitter(diagnostic.subjectId)) {
                    Select({ VfxWorkspaceSelectionKind::Emitter,
                        emitter->id, emitter->id });
                } else if (FindInput(diagnostic.subjectId) != nullptr) {
                    Select({ VfxWorkspaceSelectionKind::Input,
                        diagnostic.subjectId });
                } else if (const VFX::VfxEmitterData* owner = nullptr;
                           FindInputLink(
                               diagnostic.subjectId, &owner) != nullptr &&
                           owner != nullptr) {
                    Select({ VfxWorkspaceSelectionKind::Link,
                        diagnostic.subjectId, owner->id });
                }
            }
            if (ImGui::IsItemHovered() &&
                !diagnostic.subjectId.empty()) {
                ImGui::SetTooltip(
                    "Subject: %s", diagnostic.subjectId.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
#endif
    }

} // namespace HIKARI::EDITOR
