#include "Editor/Tools/Console/HIKARI_ConsoleCompletion.h"

#include <algorithm>
#include <array>
#include <span>

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

// 最初の語の候補は、help・clear と編集器の操作と実行時コマンドを名前順に並べたもの（同じ名前はこの順に優先する）。
// 名前が決まった後は、最後の語が当たる引数の値の候補。
void ConsoleCompletion::Update(const ConsoleSources& sources, std::string_view text) {
    if (text == candidateQuery_ || (!appliedText_.empty() && text == appliedText_))
        return;
    candidateQuery_ = text;
    appliedText_.clear();
    completionIndex_.reset();
    candidates_.clear();
    wordBegin_ = 0;
    valueHeader_.reset();
    if (text.empty())
        return;
    if (text.find_first_of(" \t") != std::string_view::npos) {
        CollectValueCandidates(sources, text);
        return;
    }
    RefreshOperationNames(sources.operations);
    const auto add = [&](std::string_view name, std::string_view summary) {
        if (StartsWithIgnoreCase(name, text))
            candidates_.push_back({std::string(name), std::string(summary)});
    };
    // コンソール自身のコマンドを先に入れる（同じ名前の実行時コマンドより優先する）。
    add(kConsoleHelpCommand, TEXT::Localize("ui.console.helpSummary"));
    add(kConsoleClearCommand, TEXT::Localize("ui.console.clearSummary"));
    for (const Candidate& operation : operationNames_)
        add(operation.name, operation.summary);
    if (sources.service != nullptr) {
        for (const RUNTIME::DebugCommand& command : sources.service->Commands())
            add(command.name, command.summary);
    }
    std::stable_sort(candidates_.begin(), candidates_.end(),
                     [](const Candidate& a, const Candidate& b) { return a.name < b.name; });
    candidates_.erase(std::unique(candidates_.begin(), candidates_.end(),
                                  [](const Candidate& a, const Candidate& b) { return a.name == b.name; }),
                      candidates_.end());
}

// 最後の語が当たる引数の schema が文字列の enum を持つなら、その語で始まる値を候補にする。どの引数に当たるかは実行
// （BuildOperationParameters）と同じ割り当てで、key=value は名前の引数、置き換えるのは = の後ろだけ。
void ConsoleCompletion::CollectValueCandidates(const ConsoleSources& sources, std::string_view text) {
    std::vector<ConsoleToken> tokens;
    std::string error;
    // 空白だけの行は語が無い。
    if (!TokenizeConsoleLine(text, tokens, error) || tokens.empty())
        return;
    const EditorOperationDefinition* operation = FindConsoleOperation(sources.operations, tokens.front().text);
    if (operation == nullptr)
        return;
    // 空白で終わっていれば、まだ打っていない次の語が補完の対象。
    if (text.back() == ' ' || text.back() == '\t')
        tokens.emplace_back();
    const std::span<const ConsoleToken> arguments = std::span<const ConsoleToken>(tokens).subspan(1);
    if (arguments.empty())
        return;
    const EditorOperationParameter* parameter = AssignArgumentParameters(*operation, arguments).back();
    if (parameter == nullptr)
        return;
    const auto choices = parameter->schema.find("enum");
    if (choices == parameter->schema.end() || !choices->is_array())
        return;
    const ConsoleToken& word = arguments.back();
    for (const nlohmann::json& choice : *choices) {
        if (choice.is_string() && StartsWithIgnoreCase(choice.get_ref<const std::string&>(), word.text))
            candidates_.push_back({choice.get<std::string>(), {}});
    }
    if (candidates_.empty())
        return;
    wordBegin_ = text.size() - word.raw.size();
    valueHeader_ = ParameterRow(*parameter);
}

std::string ConsoleCompletion::Complete(const ConsoleSources& sources, std::string_view text) {
    Update(sources, text);
    if (candidates_.empty())
        return {};
    std::string completed;
    if (candidates_.size() > 1 && !completionIndex_) {
        // 今の最後の語より伸びなければ（共通部分まで補った後の Tab）、候補を順に入れる。
        completed = CommonPrefix();
        if (completed.size() <= text.size() - wordBegin_)
            completed.clear();
    }
    if (completed.empty()) {
        completionIndex_ = completionIndex_ ? (*completionIndex_ + 1) % candidates_.size() : 0;
        completed = candidates_[*completionIndex_].name;
        // 一つに決まったら名前の後ろへ空白を足し、続けて引数を打てるようにする。
        if (candidates_.size() == 1)
            completed += ' ';
    }
    std::string applied = candidateQuery_.substr(0, wordBegin_) + completed;
    // 候補を順に回す間だけ今の候補を保つ。一つに決まった後は、次の語の候補を作り直させる。
    appliedText_ = candidates_.size() > 1 ? applied : std::string{};
    return applied;
}

ConsoleCompletion::HintRow ConsoleCompletion::ParameterRow(const EditorOperationParameter& parameter) {
    return {parameter.name,
            TEXT::EnumToName(kEditorOperationParameterTypeNames, parameter.type),
            TEXT::Localize(parameter.required ? "ui.console.required" : "ui.console.optional"),
            parameter.description,
            false,
            parameter.required};
}

// 候補の行。順に回して表示枠の外へ出た候補も見えるよう、選択中の行が枠の末尾に来るまで送る。
void ConsoleCompletion::AppendCandidateRows(std::vector<HintRow>& rows) const {
    const size_t shown = (std::min)(candidates_.size(), kVisibleCandidates);
    const size_t first =
        completionIndex_ && *completionIndex_ >= kVisibleCandidates ? *completionIndex_ + 1 - kVisibleCandidates : 0;
    for (size_t index = first; index < first + shown; ++index)
        rows.push_back({candidates_[index].name, {}, {}, candidates_[index].summary, completionIndex_ == index, false});
    if (candidates_.size() > shown)
        rows.push_back({"... +" + std::to_string(candidates_.size() - shown), {}, {}, {}, false, false});
}

std::vector<ConsoleCompletion::HintRow> ConsoleCompletion::BuildHintRows(const ConsoleSources& sources,
                                                                         std::string_view text) const {
    std::vector<HintRow> rows;
    const size_t nameEnd = text.find_first_of(" \t");
    if (nameEnd == std::string_view::npos) {
        AppendCandidateRows(rows);
        return rows;
    }
    // 引数の値に候補がある間は、その引数の行と候補を出す。
    if (valueHeader_) {
        rows.push_back(*valueHeader_);
        AppendCandidateRows(rows);
        return rows;
    }
    const std::string_view name = text.substr(0, nameEnd);
    if (const EditorOperationDefinition* operation = FindConsoleOperation(sources.operations, name)) {
        rows.push_back({operation->id, {}, {}, operation->summary, true, false});
        const size_t shown = (std::min)(operation->parameters.size(), kVisibleParameters);
        for (size_t index = 0; index < shown; ++index)
            rows.push_back(ParameterRow(operation->parameters[index]));
        if (operation->parameters.size() > shown)
            rows.push_back({"... +" + std::to_string(operation->parameters.size() - shown), {}, {}, {}, false, false});
    } else if (sources.service != nullptr) {
        for (const RUNTIME::DebugCommand& command : sources.service->Commands()) {
            if (command.name == name)
                rows.push_back({command.usage, {}, {}, command.summary, true, false});
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
