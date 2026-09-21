#include "Editor/Diagnostics/HIKARI_EditorLogPanel.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Platform/HIKARI_EditorFileDialog.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include <algorithm>
#include <deque>
#include <fstream>
#include <unordered_map>
#include "imgui.h"

namespace HIKARI::EDITOR {
    namespace {
        std::string Text(const char* key) { return TEXT::Localize(key); }
        const char* LevelKey(CORE::LogLevel level) {
            switch (level) {
            case CORE::LogLevel::Warn: return "log.warning";
            case CORE::LogLevel::Error: return "log.error";
            case CORE::LogLevel::D3D12: return "log.graphics";
            default: return "log.info";
            }
        }
        const char* LevelTag(CORE::LogLevel level) {
            switch (level) {
            case CORE::LogLevel::Warn: return "WARN";
            case CORE::LogLevel::Error: return "ERROR";
            case CORE::LogLevel::D3D12: return "D3D12";
            default: return "INFO";
            }
        }
    }
    void EditorLogPanel::Poll() {
        if (paused_ || history_) return;
        auto next = CORE::ReadLogHistory(cursor_);
        if (next.empty()) return;
        cursor_ = next.back().sequence;
        records_.insert(records_.end(), std::make_move_iterator(next.begin()), std::make_move_iterator(next.end()));
        const auto capacity = CORE::GetLogHistoryInfo().capacity;
        if (records_.size() > capacity) records_.erase(records_.begin(), records_.end() - capacity);
        BuildRows();
    }
    void EditorLogPanel::BuildRows() {
        rows_.clear();
        std::unordered_map<std::string, size_t> groups;
        for (const auto& record : records_) {
            const auto level = static_cast<size_t>(record.level);
            if (level >= 4 || !levels_[level]) continue;
            if (search_[0] && record.message.find(search_) == std::string::npos && record.source.find(search_) == std::string::npos) continue;
            if (collapse_) {
                auto key = std::to_string(level) + '\n' + record.source + '\n' + record.message + '\n' + record.resource;
                const auto [it, inserted] = groups.emplace(std::move(key), rows_.size());
                if (!inserted) { ++rows_[it->second].count; continue; }
            }
            rows_.push_back({record, 1});
        }
    }
    void EditorLogPanel::OpenHistory() {
        const auto path = FILE_DIALOG::PickFile(CORE::GetLogFilePath().parent_path(), L"Log", L"*.log;*.txt");
        if (!path) return;
        std::ifstream input(*path);
        if (!input) { status_ = Text("log.readFailed"); return; }
        std::deque<std::string> tail;
        const auto capacity = CORE::GetLogHistoryInfo().capacity;
        std::string line;
        while (std::getline(input, line)) { tail.push_back(std::move(line)); if (tail.size() > capacity) tail.pop_front(); }
        records_.clear(); uint64_t id = 0;
        for (auto& message : tail) {
            const auto level = message.find("[ERROR]") != std::string::npos ? CORE::LogLevel::Error :
                message.find("[WARN]") != std::string::npos ? CORE::LogLevel::Warn :
                message.find("[D3D12]") != std::string::npos ? CORE::LogLevel::D3D12 : CORE::LogLevel::Info;
            records_.push_back({++id, level, {}, "File", std::move(message), {}});
        }
        history_ = true; selected_ = 0; status_.clear(); BuildRows();
    }
    void EditorLogPanel::ExportVisible() {
        const auto folder = FILE_DIALOG::PickFolder(CORE::GetLogFilePath().parent_path());
        if (!folder) return;
        std::filesystem::path path;
        for (unsigned suffix = 1;; ++suffix) {
            path = *folder / ("HIKARI-log-" + std::to_string(suffix) + ".log");
            if (!std::filesystem::exists(path)) break;
        }
        std::ofstream output(path);
        for (const auto& row : rows_) output << row.record.timestamp << " [" << LevelTag(row.record.level) << "] [" << row.record.source << "] " << row.record.message << " (x" << row.count << ")\n";
        output.close();
        if (!output) status_ = Text("log.exportFailed");
        else { status_.clear(); SHELL::ShowFileInExplorer(path); }
    }
    void EditorLogPanel::DrawContents() {
        const auto previous = cursor_; Poll();
        const auto button = [&](EditorGlyph glyph, const char* id, const char* key) {
            return IconButton(glyph, id, EditorButtonTone::Quiet, {28,28}, Text(key).c_str());
        };
        if (button(paused_ ? EditorGlyph::Play : EditorGlyph::Pause, "pauseLog", paused_ ? "log.resume" : "log.pause")) paused_ = !paused_;
        NextToolbarItem();
        if (IconToggleButton(EditorGlyph::Focus, "followLog", follow_, {28,28}, Text("log.follow").c_str())) follow_ = !follow_;
        NextToolbarItem();
        if (IconToggleButton(EditorGlyph::Compact, "collapseLog", collapse_, {28,28}, Text("log.collapse").c_str())) {collapse_ = !collapse_; BuildRows();}
        NextToolbarItem();
        if (button(EditorGlyph::Delete, "clearLogView", "log.clearView")) {records_.clear(); rows_.clear(); selected_=0; cursor_=CORE::GetLogHistoryInfo().latestSequence;}
        NextToolbarItem();
        if (button(EditorGlyph::Folder, "openLogHistory", "log.openHistory")) OpenHistory();
        NextToolbarItem();
        if (button(EditorGlyph::SaveAs, "exportLog", "log.export")) ExportVisible();
        NextToolbarItem();
        if (button(EditorGlyph::Reveal, "revealLogFile", "log.openFile")) SHELL::ShowFileInExplorer(CORE::GetLogFilePath());
        if (history_ && IconTextButton(EditorGlyph::Refresh, Text("log.live").c_str(), "liveLog")) {history_=false; records_.clear(); cursor_=0; Poll();}
        if (SearchField("logSearch", Text("log.search").c_str(), search_, sizeof(search_))) BuildRows();
        for (size_t i=0; i<4; ++i) {
            if (i) NextToolbarItem(ImGui::GetFrameHeight() + ImGui::CalcTextSize(Text(LevelKey(static_cast<CORE::LogLevel>(i))).c_str()).x + 6);
            ImGui::PushID(static_cast<int>(i));
            const auto label = Text(LevelKey(static_cast<CORE::LogLevel>(i))) + "###level";
            if (ImGui::Checkbox(label.c_str(), &levels_[i])) BuildRows();
            ImGui::PopID();
        }
        if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
        const Row* selected = nullptr;
        for (const auto& row : rows_) if (row.record.sequence == selected_) selected = &row;
        const auto& theme = GetEditorThemePalette();
        const float details = selected ? (std::min)(180.0f, ImGui::GetContentRegionAvail().y * 0.4f) : 0;
        if (ImGui::BeginChild("logRows", {0, -details}, true)) {
            ImGuiListClipper clipper; clipper.Begin(static_cast<int>(rows_.size()));
            while (clipper.Step()) for (int i=clipper.DisplayStart; i<clipper.DisplayEnd; ++i) {
                const auto& row = rows_[i];
                ImGui::PushID(static_cast<int>(row.record.sequence));
                const auto color = row.record.level == CORE::LogLevel::Error ? theme.error : row.record.level == CORE::LogLevel::Warn ? theme.warning : theme.text;
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                auto label = row.record.timestamp.size() > 11 ? row.record.timestamp.substr(11) + "  " : std::string{};
                label += "[" + row.record.source + "] " + row.record.message.substr(0, row.record.message.find('\n'));
                if (row.count > 1) label += "  ×" + std::to_string(row.count);
                if (ImGui::Selectable((label+"###record").c_str(), row.record.sequence == selected_)) selected_ = row.record.sequence;
                ImGui::PopStyleColor(); ImGui::PopID();
            }
            if (follow_ && previous != cursor_) ImGui::SetScrollHereY(1);
        }
        ImGui::EndChild();
        if (selected) {
            ImGui::Separator();
            if (button(EditorGlyph::Duplicate, "copyLogRecord", "log.copy")) ImGui::SetClipboardText(selected->record.message.c_str());
            if (!selected->record.resource.empty()) {
                NextToolbarItem();
                if (button(EditorGlyph::Reveal, "revealLogResource", "log.revealResource")) SHELL::ShowFileInExplorer(TEXT::PathFromUtf8(selected->record.resource));
            }
            ImGui::BeginChild("logDetail"); ImGui::TextWrapped("%s", selected->record.message.c_str()); ImGui::EndChild();
        }
    }
}
