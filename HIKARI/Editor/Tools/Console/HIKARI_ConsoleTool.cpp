#include "Editor/Tools/Console/HIKARI_ConsoleTool.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_Trim.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Tools/Console/HIKARI_ConsoleCommandLine.h"
#include "Editor/Tools/Console/HIKARI_ConsoleCompletion.h"
#include "Editor/Tools/Console/HIKARI_ConsoleHistory.h"
#include "Runtime/Diagnostics/HIKARI_RuntimeDebugService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_World.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {

constexpr size_t kMaximumOutputLines = 400;
constexpr size_t kMaximumDataBytes = 300;

enum class LineKind : uint8_t { Command, Success, Failure, Plain, Muted };

struct ConsoleLine {
    LineKind kind = LineKind::Plain;
    std::string text{};
};

RUNTIME::RuntimeDebugService* FindDebugService(DocumentSceneBase& scene) {
    return scene.GetWorld().Services().Find<RUNTIME::RuntimeDebugService>();
}

ConsoleSources SourcesOf(EditorToolContext& context) { return {context.operations, FindDebugService(context.scene)}; }

// 結果の data を一行にする。長ければ UTF-8 の境界で切る。
std::string CompactJson(const nlohmann::json& data) {
    std::string text = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const size_t keep = TEXT::Utf8PrefixByteCount(text, kMaximumDataBytes);
    if (keep < text.size())
        text = text.substr(0, keep) + "...";
    return text;
}

ImVec4 LineColor(const EditorThemePalette& palette, LineKind kind) {
    switch (kind) {
    case LineKind::Command: return palette.accent;
    case LineKind::Success: return palette.success;
    case LineKind::Failure: return palette.error;
    case LineKind::Muted: return palette.textMuted;
    case LineKind::Plain: break;
    }
    return palette.text;
}

void ReplaceText(ImGuiInputTextCallbackData& data, std::string_view text) {
    data.DeleteChars(0, data.BufTextLen);
    data.InsertChars(0, text.data(), text.data() + text.size());
}

class ConsoleTool final : public IEditorTool {
public:
    void Draw(EditorToolContext& context, bool& open) override;

private:
    // InputText のコールバックへ渡すもの。
    struct InputEvent {
        ConsoleTool& tool;
        EditorToolContext& context;
    };
    static int OnInputEvent(ImGuiInputTextCallbackData* data);
    int HandleInput(ImGuiInputTextCallbackData& data, EditorToolContext& context);

    void SyncHistory(DocumentSceneBase& scene);
    void TakeTraceLines(DocumentSceneBase& scene);
    void Append(LineKind kind, std::string text);
    void DrawOutput();
    void DrawInput(EditorToolContext& context);

    void Submit(EditorToolContext& context);
    void RunOperation(EditorToolContext& context, const EditorOperationDefinition& operation, const std::string& line);
    void RunRuntimeCommand(EditorToolContext& context, const std::string& line);
    void AppendResult(const EditorOperationResult& result);

    ConsoleHistory history_{};
    std::filesystem::path historyLibraryRoot_{};
    ConsoleCompletion completion_{};
    std::deque<ConsoleLine> output_{};
    bool scrollToBottom_ = false;
    bool focusInput_ = false;
    char input_[ConsoleHistory::kMaximumLineBytes + 1]{};
};

void ConsoleTool::Draw(EditorToolContext& context, bool& open) {
    const std::string windowName = TEXT::LocalizeWindow("ui.window.console", "Console");
    if (context.openRequest != nullptr) {
        RevealEditorWindow(windowName.c_str());
        focusInput_ = true;
    }
    ImGui::SetNextWindowSize({720.0f, 360.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(windowName.c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }
    SyncHistory(context.scene);
    TakeTraceLines(context.scene);
    DrawOutput();
    DrawInput(context);
    ImGui::End();
}

// 履歴はプロジェクトごと。開いているプロジェクトが替わったら読み直す。
void ConsoleTool::SyncHistory(DocumentSceneBase& scene) {
    const std::filesystem::path& libraryRoot = scene.GetAssetDatabase().GetLibraryRoot();
    if (libraryRoot == historyLibraryRoot_)
        return;
    historyLibraryRoot_ = libraryRoot;
    history_.Load(libraryRoot / "Editor" / "ConsoleHistory.txt");
}

// trace が拾ったイベントは実行時コマンドの返事とは別に、後から届く。
void ConsoleTool::TakeTraceLines(DocumentSceneBase& scene) {
    if (RUNTIME::RuntimeDebugService* service = FindDebugService(scene)) {
        for (std::string& line : service->TakeTraceLines())
            Append(LineKind::Plain, std::move(line));
    }
}

void ConsoleTool::Append(LineKind kind, std::string text) {
    output_.push_back({kind, std::move(text)});
    if (output_.size() > kMaximumOutputLines)
        output_.pop_front();
}

void ConsoleTool::DrawOutput() {
    const EditorThemePalette& palette = GetEditorThemePalette();
    ImGui::BeginChild("##ConsoleOutput", {0.0f, -ImGui::GetFrameHeightWithSpacing()}, ImGuiChildFlags_Border);
    // 末尾を見ている間だけ、増えた行に追従する。
    const bool followTail = scrollToBottom_ || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
    scrollToBottom_ = false;
    ImGui::PushTextWrapPos(0.0f);
    if (output_.empty())
        ImGui::TextDisabled("%s", TEXT::Localize("ui.console.empty").c_str());
    for (const ConsoleLine& line : output_) {
        ImGui::PushStyleColor(ImGuiCol_Text, LineColor(palette, line.kind));
        ImGui::TextUnformatted(line.text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    if (followTail)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void ConsoleTool::DrawInput(EditorToolContext& context) {
    constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll |
                                          ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory |
                                          ImGuiInputTextFlags_CallbackCharFilter;
    ImGui::SetNextItemWidth(-1.0f);
    if (focusInput_) {
        ImGui::SetKeyboardFocusHere();
        focusInput_ = false;
    }
    InputEvent event{*this, context};
    const bool submitted = ImGui::InputTextWithHint("##ConsoleInput", TEXT::Localize("ui.console.inputHint").c_str(),
                                                    input_, sizeof(input_), flags, &ConsoleTool::OnInputEvent, &event);
    const bool active = ImGui::IsItemActive();
    const ImVec2 inputMin = ImGui::GetItemRectMin();
    const float inputWidth = ImGui::GetItemRectSize().x;
    if (submitted) {
        Submit(context);
        // Enter で入力欄の操作が終わるので、続けて打てるように戻す。
        ImGui::SetKeyboardFocusHere(-1);
    }
    if (active)
        completion_.DrawHints(SourcesOf(context), input_, inputMin, inputWidth);
}

int ConsoleTool::OnInputEvent(ImGuiInputTextCallbackData* data) {
    InputEvent& event = *static_cast<InputEvent*>(data->UserData);
    return event.tool.HandleInput(*data, event.context);
}

int ConsoleTool::HandleInput(ImGuiInputTextCallbackData& data, EditorToolContext& context) {
    const std::string_view text(data.Buf, static_cast<size_t>(data.BufTextLen));
    switch (data.EventFlag) {
    case ImGuiInputTextFlags_CallbackCharFilter:
        // ` と ~ は開閉キーの文字なので、入力欄には入れない。
        return data.EventChar == '`' || data.EventChar == '~' ? 1 : 0;
    case ImGuiInputTextFlags_CallbackHistory:
        if (const std::string* line = history_.Step(data.EventKey == ImGuiKey_UpArrow ? -1 : 1, text))
            ReplaceText(data, *line);
        break;
    case ImGuiInputTextFlags_CallbackCompletion:
        if (const std::string completed = completion_.Complete(SourcesOf(context), text); !completed.empty())
            ReplaceText(data, completed);
        break;
    default: break;
    }
    return 0;
}

void ConsoleTool::Submit(EditorToolContext& context) {
    const std::string line = TEXT::TrimCopy(input_);
    input_[0] = '\0';
    history_.Add(line);
    if (line.empty())
        return;
    scrollToBottom_ = true;
    if (line == kConsoleClearCommand) {
        output_.clear();
        return;
    }
    Append(LineKind::Command, "> " + line);
    // 先頭の語が人に見える操作なら操作として、そうでなければ実行時コマンドとして実行する。
    const std::string_view name = std::string_view(line).substr(0, line.find_first_of(" \t"));
    if (const EditorOperationDefinition* operation = FindConsoleOperation(context.operations, name))
        RunOperation(context, *operation, line);
    else
        RunRuntimeCommand(context, line);
}

void ConsoleTool::RunOperation(EditorToolContext& context, const EditorOperationDefinition& operation,
                               const std::string& line) {
    std::vector<ConsoleToken> tokens;
    nlohmann::json parameters;
    std::string error;
    if (!TokenizeConsoleLine(line, tokens, error) ||
        !BuildOperationParameters(operation, std::span<const ConsoleToken>(tokens).subspan(1), parameters, error)) {
        Append(LineKind::Failure, error);
        return;
    }
    AppendResult(context.operations.Execute(operation.id, parameters));
}

void ConsoleTool::RunRuntimeCommand(EditorToolContext& context, const std::string& line) {
    RUNTIME::RuntimeDebugService* service = FindDebugService(context.scene);
    if (service == nullptr) {
        Append(LineKind::Failure, "Runtime commands are unavailable: this World has no debug service.");
        return;
    }
    // 返事は実行時側の書式のまま見せる。
    const RUNTIME::DebugReply reply = service->Execute(context.scene.GetWorld(), line);
    Append(reply.success ? LineKind::Plain : LineKind::Failure, reply.text);
}

void ConsoleTool::AppendResult(const EditorOperationResult& result) {
    std::string head = result.ok ? "[OK] " : "[FAILED] ";
    head += result.code;
    if (!result.message.empty())
        head += ": " + result.message;
    Append(result.ok ? LineKind::Success : LineKind::Failure, std::move(head));
    // 登録表が全ての結果に付ける操作の種別は、人には要らない。
    nlohmann::json data = result.data;
    if (data.is_object())
        data.erase("operationKind");
    if (!data.empty())
        Append(LineKind::Plain, "  " + CompactJson(data));
    for (const std::string& detail : result.details)
        Append(LineKind::Muted, "  " + detail);
    for (const std::string& hint : result.hints)
        Append(LineKind::Muted, "  hint: " + hint);
}

} // namespace

std::unique_ptr<IEditorTool> CreateConsoleTool() { return std::make_unique<ConsoleTool>(); }

} // namespace HIKARI::EDITOR
