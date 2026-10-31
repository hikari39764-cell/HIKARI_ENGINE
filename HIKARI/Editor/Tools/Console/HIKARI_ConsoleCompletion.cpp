#include "Editor/Tools/Console/HIKARI_ConsoleCompletion.h"

#include <algorithm>
#include <array>

#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Tools/Console/HIKARI_ConsoleCommandLine.h"
#include "Runtime/Diagnostics/HIKARI_RuntimeDebugService.h"

namespace HIKARI::EDITOR {
namespace {

constexpr size_t kVisibleCandidates = 8;
constexpr size_t kVisibleParameters = 12;

bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), text.begin(), [](char a, char b) {
               return TEXT::ToLowerAscii(a) == TEXT::ToLowerAscii(b);
           });
}

} // namespace

std::string ConsoleCompletion::CommonPrefix() const {
    std::string prefix = candidates_.front().name;
    for (const Candidate& candidate : candidates_) {
        size_t length = 0;
        while (length < prefix.size() && length < candidate.name.size() && prefix[length] == candidate.name[length])
            ++length;
        prefix.resize(length);
    }
    return prefix;
}

void ConsoleCompletion::RefreshOperationNames(const EditorOperationRegistry& operations) {
    if (operationRevision_ == operations.Revision())
        return;
    operationRevision_ = operations.Revision();
    operationNames_.clear();
    for (const std::string& id : operations.RegisteredIds()) {
        if (const EditorOperationDefinition* operation = FindConsoleOperation(operations, id))
            operationNames_.push_back({id, operation->summary});
    }
}

// 候補は編集器の操作と実行時コマンドと clear を名前順に並べたもの。同じ名前は操作を優先する。
void ConsoleCompletion::Update(const ConsoleSources& sources, std::string_view text) {
    if (text == candidateQuery_ || (!appliedText_.empty() && text == appliedText_))
        return;
    candidateQuery_ = text;
    appliedText_.clear();
    completionIndex_.reset();
    candidates_.clear();
    // 候補を出すのは最初の語を打っている間だけ。
    if (text.empty() || text.find_first_of(" \t") != std::string_view::npos)
        return;
    RefreshOperationNames(sources.operations);
    const auto add = [&](std::string_view name, std::string_view summary) {
        if (StartsWithIgnoreCase(name, text))
            candidates_.push_back({std::string(name), std::string(summary)});
    };
    for (const Candidate& operation : operationNames_)
        add(operation.name, operation.summary);
    if (sources.service != nullptr) {
        for (const RUNTIME::DebugCommand& command : sources.service->Commands())
            add(command.name, command.usage);
    }
    add(kConsoleClearCommand, TEXT::Localize("ui.console.clearSummary"));
    std::stable_sort(candidates_.begin(), candidates_.end(),
                     [](const Candidate& a, const Candidate& b) { return a.name < b.name; });
    candidates_.erase(std::unique(candidates_.begin(), candidates_.end(),
                                  [](const Candidate& a, const Candidate& b) { return a.name == b.name; }),
                      candidates_.end());
}

std::string ConsoleCompletion::Complete(const ConsoleSources& sources, std::string_view text) {
    Update(sources, text);
    if (candidates_.empty())
        return {};
    std::string completed;
    if (candidates_.size() > 1 && !completionIndex_) {
        completed = CommonPrefix();
        if (completed.size() <= text.size())
            completed.clear();
    }
    if (completed.empty()) {
        completionIndex_ = completionIndex_ ? (*completionIndex_ + 1) % candidates_.size() : 0;
        completed = candidates_[*completionIndex_].name;
        // 一つに決まったら名前の後ろへ空白を足し、続けて引数を打てるようにする。
        if (candidates_.size() == 1)
            completed += ' ';
    }
    appliedText_ = completed;
    return completed;
}

std::vector<ConsoleCompletion::HintRow> ConsoleCompletion::BuildHintRows(const ConsoleSources& sources,
                                                                         std::string_view text) const {
    std::vector<HintRow> rows;
    const size_t nameEnd = text.find_first_of(" \t");
    if (nameEnd == std::string_view::npos) {
        const size_t shown = (std::min)(candidates_.size(), kVisibleCandidates);
        // 順に回して表示枠の外へ出た候補も見えるよう、選択中の行が枠の末尾に来るまで送る。
        const size_t first = completionIndex_ && *completionIndex_ >= kVisibleCandidates
                                 ? *completionIndex_ + 1 - kVisibleCandidates
                                 : 0;
        for (size_t index = first; index < first + shown; ++index)
            rows.push_back(
                {candidates_[index].name, {}, {}, candidates_[index].summary, completionIndex_ == index, false});
        if (candidates_.size() > shown)
            rows.push_back({"... +" + std::to_string(candidates_.size() - shown), {}, {}, {}, false, false});
        return rows;
    }
    const std::string_view name = text.substr(0, nameEnd);
    if (const EditorOperationDefinition* operation = FindConsoleOperation(sources.operations, name)) {
        rows.push_back({operation->id, {}, {}, operation->summary, true, false});
        const size_t shown = (std::min)(operation->parameters.size(), kVisibleParameters);
        for (size_t index = 0; index < shown; ++index) {
            const EditorOperationParameter& parameter = operation->parameters[index];
            rows.push_back({parameter.name, TEXT::EnumToName(kEditorOperationParameterTypeNames, parameter.type),
                            TEXT::Localize(parameter.required ? "ui.console.required" : "ui.console.optional"),
                            parameter.description, false, parameter.required});
        }
        if (operation->parameters.size() > shown)
            rows.push_back({"... +" + std::to_string(operation->parameters.size() - shown), {}, {}, {}, false, false});
    } else if (sources.service != nullptr) {
        for (const RUNTIME::DebugCommand& command : sources.service->Commands()) {
            if (command.name == name)
                rows.push_back({command.usage, {}, {}, {}, true, false});
        }
    }
    return rows;
}

void ConsoleCompletion::DrawHints(const ConsoleSources& sources, std::string_view text, const ImVec2& inputMin,
                                  float width) {
    Update(sources, text);
    const std::vector<HintRow> rows = BuildHintRows(sources, text);
    if (rows.empty())
        return;
    const EditorThemePalette& palette = GetEditorThemePalette();
    const ImGuiStyle& style = ImGui::GetStyle();
    std::array<float, 3> columns{};
    for (const HintRow& row : rows) {
        columns[0] = (std::max)(columns[0], ImGui::CalcTextSize(row.name.c_str()).x);
        columns[1] = (std::max)(columns[1], ImGui::CalcTextSize(row.type.c_str()).x);
        columns[2] = (std::max)(columns[2], ImGui::CalcTextSize(row.flag.c_str()).x);
    }
    const float gap = style.ItemSpacing.x * 2.0f;
    const float height = static_cast<float>(rows.size()) * ImGui::GetTextLineHeightWithSpacing() +
                         style.WindowPadding.y * 2.0f - style.ItemSpacing.y;
    ImGui::SetCursorScreenPos({inputMin.x, inputMin.y - height - style.ItemSpacing.y});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.raised);
    ImGui::BeginChild("##ConsoleHints", {width, height}, ImGuiChildFlags_Border,
                      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const float typeX = ImGui::GetCursorPosX() + columns[0] + gap;
    const float flagX = typeX + (columns[1] > 0.0f ? columns[1] + gap : 0.0f);
    const float textX = flagX + (columns[2] > 0.0f ? columns[2] + gap : 0.0f);
    for (const HintRow& row : rows) {
        ImGui::TextColored(row.highlighted ? palette.text : palette.accent, "%s", row.name.c_str());
        if (!row.type.empty()) {
            ImGui::SameLine(typeX);
            ImGui::TextColored(palette.textMuted, "%s", row.type.c_str());
        }
        if (!row.flag.empty()) {
            ImGui::SameLine(flagX);
            ImGui::TextColored(row.required ? palette.warning : palette.textMuted, "%s", row.flag.c_str());
        }
        if (!row.text.empty()) {
            ImGui::SameLine(textX);
            ImGui::TextUnformatted(row.text.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace HIKARI::EDITOR
