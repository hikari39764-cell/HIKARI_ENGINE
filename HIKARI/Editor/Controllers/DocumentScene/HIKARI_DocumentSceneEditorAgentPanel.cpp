#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_Trim.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Agent/HIKARI_AgentProjectFiles.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
namespace {

using EDITOR::AGENT::AgentKind;

constexpr size_t kMaximumAgentOperations = 50u;
// 依頼に添える编辑器现场の上限（文脈を圧迫しないため）。
constexpr size_t kMaximumAttachedStateBytes = 6000u;
constexpr size_t kMaximumConversations = 20u;
constexpr size_t kConversationTitleBytes = 60u;

size_t AgentIndex(AgentKind kind) { return kind == AgentKind::ClaudeCode ? 0u : 1u; }

int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// UTF-8 の文字の途中で切らない。
std::string ClipTitle(const std::string& text) {
    if (text.size() <= kConversationTitleBytes)
        return text;
    size_t end = kConversationTitleBytes;
    while (end > 0u && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u)
        --end;
    return text.substr(0u, end);
}

void LoadConversations(EDITOR::AGENT::AgentPanelState& panel, const std::filesystem::path& file) {
    panel.conversations.clear();
    panel.conversationsFile = file;
    std::ifstream input(file, std::ios::binary);
    const nlohmann::json list = nlohmann::json::parse(input, nullptr, false);
    if (!list.is_array())
        return;
    for (const nlohmann::json& entry : list) {
        if (!entry.is_object())
            continue;
        EDITOR::AGENT::AgentPanelState::Conversation conversation;
        if (!TEXT::TryParseEnum(EDITOR::AGENT::kAgentKindNames, entry.value("kind", std::string{}), conversation.kind))
            continue;
        conversation.id = entry.value("id", std::string{});
        conversation.title = entry.value("title", std::string{});
        conversation.updatedAt = entry.value("updatedAt", int64_t{0});
        if (!conversation.id.empty())
            panel.conversations.push_back(std::move(conversation));
    }
}

void SaveConversations(const EDITOR::AGENT::AgentPanelState& panel) {
    if (panel.conversationsFile.empty())
        return;
    nlohmann::json list = nlohmann::json::array();
    for (const auto& conversation : panel.conversations)
        list.push_back({{"kind", TEXT::EnumToName(EDITOR::AGENT::kAgentKindNames, conversation.kind)},
                        {"id", conversation.id},
                        {"title", conversation.title},
                        {"updatedAt", conversation.updatedAt}});
    std::error_code error;
    std::filesystem::create_directories(panel.conversationsFile.parent_path(), error);
    std::ofstream(panel.conversationsFile, std::ios::binary | std::ios::trunc)
        << list.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

// 今の会話を一覧の先頭に置く。題は最初の依頼。
void RememberConversation(EDITOR::AGENT::AgentPanelState& panel) {
    const std::string& id = panel.session.SessionId();
    if (id.empty())
        return;
    auto& list = panel.conversations;
    const auto found = std::find_if(list.begin(), list.end(), [&id](const auto& entry) { return entry.id == id; });
    EDITOR::AGENT::AgentPanelState::Conversation conversation =
        found != list.end() ? *found : EDITOR::AGENT::AgentPanelState::Conversation{panel.session.Kind(), id};
    if (found != list.end())
        list.erase(found);
    if (conversation.title.empty()) {
        const auto& events = panel.session.Events();
        const auto first = std::find_if(events.begin(), events.end(), [](const EDITOR::AGENT::AgentEvent& event) {
            return event.kind == EDITOR::AGENT::AgentEvent::Kind::User;
        });
        if (first != events.end())
            conversation.title = ClipTitle(first->text);
    }
    conversation.updatedAt = NowSeconds();
    list.insert(list.begin(), std::move(conversation));
    if (list.size() > kMaximumConversations)
        list.resize(kMaximumConversations);
    SaveConversations(panel);
}

bool SameSettings(const EDITOR::AGENT::AgentSessionRequest& lhs, const EDITOR::AGENT::AgentSessionRequest& rhs) {
    // Codex は模型と推理强度を一轮ごとに turn/start で渡すので、会話を起動し直さない。
    const bool sameModel = lhs.kind == AgentKind::Codex || (lhs.model == rhs.model && lhs.effort == rhs.effort);
    return lhs.kind == rhs.kind && lhs.executable == rhs.executable && lhs.allowProjectEdits == rhs.allowProjectEdits &&
           sameModel && lhs.chrome == rhs.chrome && lhs.userTools == rhs.userTools &&
           lhs.autoApprove == rhs.autoApprove && lhs.projectRoot == rhs.projectRoot;
}

void SavePreferences(const EDITOR::AGENT::AgentPanelState& panel) {
    if (panel.preferencesFile.empty())
        return;
    nlohmann::json value{{"agent", TEXT::EnumToName(EDITOR::AGENT::kAgentKindNames, panel.kind)}};
    for (const auto& entry : EDITOR::AGENT::kAgentKindNames) {
        const auto& [model, effort] = panel.remembered[AgentIndex(entry.value)];
        value[std::string(entry.name)] = {{"model", model}, {"effort", effort}};
    }
    nlohmann::json models = nlohmann::json::array();
    for (const EDITOR::AGENT::AgentModelOption& option : panel.codexModels)
        models.push_back({{"id", option.id},
                          {"label", option.label},
                          {"efforts", option.efforts},
                          {"defaultEffort", option.defaultEffort},
                          {"isDefault", option.isDefault}});
    value["Codex"]["models"] = std::move(models);
    std::error_code error;
    std::filesystem::create_directories(panel.preferencesFile.parent_path(), error);
    std::ofstream(panel.preferencesFile, std::ios::binary | std::ios::trunc)
        << value.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

// 今の智能体の選択を入力欄へ写す。
void ApplyRemembered(EDITOR::AGENT::AgentPanelState& panel) {
    const auto& [model, effort] = panel.remembered[AgentIndex(panel.kind)];
    std::snprintf(panel.model, sizeof(panel.model), "%s", model.c_str());
    std::snprintf(panel.effort, sizeof(panel.effort), "%s", effort.c_str());
}

void LoadPreferences(EDITOR::AGENT::AgentPanelState& panel, const std::filesystem::path& file) {
    panel.preferencesFile = file;
    std::ifstream input(file, std::ios::binary);
    const nlohmann::json value = nlohmann::json::parse(input, nullptr, false);
    if (value.is_object()) {
        (void)TEXT::TryParseEnum(EDITOR::AGENT::kAgentKindNames, value.value("agent", std::string{}), panel.kind);
        for (const auto& entry : EDITOR::AGENT::kAgentKindNames) {
            const nlohmann::json settings = value.value(std::string(entry.name), nlohmann::json::object());
            if (settings.is_object())
                panel.remembered[AgentIndex(entry.value)] = {settings.value("model", std::string{}),
                                                             settings.value("effort", std::string{})};
        }
        panel.codexModels.clear();
        const nlohmann::json codex = value.value("Codex", nlohmann::json::object());
        const nlohmann::json models =
            codex.is_object() ? codex.value("models", nlohmann::json::array()) : nlohmann::json::array();
        for (const nlohmann::json& model : models) {
            if (model.is_object())
                panel.codexModels.push_back({model.value("id", std::string{}), model.value("label", std::string{}),
                                             model.value("efforts", std::vector<std::string>{}),
                                             model.value("defaultEffort", std::string{}),
                                             model.value("isDefault", false)});
        }
    }
    ApplyRemembered(panel);
}

#if defined(HIKARI_WITH_EDITOR)
std::string FormatTokens(uint64_t tokens) {
    char text[32]{};
    if (tokens >= 1000000u)
        std::snprintf(text, sizeof(text), "%.1fM", static_cast<double>(tokens) / 1000000.0);
    else if (tokens >= 1000u)
        std::snprintf(text, sizeof(text), "%.1fk", static_cast<double>(tokens) / 1000.0);
    else
        std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(tokens));
    return text;
}

// 重置までの残り（「3h 20m」）。
std::string FormatRemaining(int64_t resetsAt) {
    const int64_t seconds = (std::max)(int64_t{0}, resetsAt - NowSeconds());
    char text[32]{};
    if (seconds >= 86400)
        std::snprintf(text, sizeof(text), "%lldd %lldh", static_cast<long long>(seconds / 86400),
                      static_cast<long long>(seconds % 86400 / 3600));
    else
        std::snprintf(text, sizeof(text), "%lldh %lldm", static_cast<long long>(seconds / 3600),
                      static_cast<long long>(seconds % 3600 / 60));
    return text;
}

void DrawAgentEvent(const EDITOR::AGENT::AgentEvent& event) {
    using Kind = EDITOR::AGENT::AgentEvent::Kind;
    switch (event.kind) {
    case Kind::User: ImGui::SeparatorText(ClipTitle(event.text).c_str()); break;
    case Kind::Text: ImGui::TextWrapped("%s", event.text.c_str()); break;
    case Kind::ToolCall:
        ImGui::TextDisabled("%s %s", TEXT::Localize("agent.event.tool").c_str(), event.title.c_str());
        if (!event.text.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", event.text.c_str());
        break;
    case Kind::ToolResult:
        if (event.failed)
            ImGui::TextColored({1.0f, 0.55f, 0.3f, 1.0f}, "  %s", event.text.substr(0, 160).c_str());
        break;
    case Kind::Result:
        ImGui::TextDisabled("%s · %s",
                            TEXT::Localize(event.failed ? "agent.event.error" : "agent.event.result").c_str(),
                            event.title.c_str());
        // 失敗の理由（未登录など）は CLI の文のまま見せる。
        if (event.failed && !event.text.empty())
            ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "%s", event.text.c_str());
        break;
    case Kind::Error:
        ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "%s %s", TEXT::Localize("agent.event.error").c_str(),
                           event.text.c_str());
        break;
    case Kind::Blocked:
        ImGui::TextColored({1.0f, 0.62f, 0.3f, 1.0f}, "%s %s",
                           TEXT::Localize("agent.event.blocked." + event.title).c_str(), event.text.c_str());
        break;
    case Kind::AutoApproved:
        ImGui::TextDisabled("%s %s", TEXT::Localize("agent.event.autoApproved").c_str(), event.text.c_str());
        break;
    case Kind::Raw: break;
    }
}

// 承認の選び方。既定は「本轮允许」（左端）。毎回聞く操作には出さない。
template <class Resolve>
void DrawDecisionButtons(bool allowTurn, Resolve resolve) {
    if (allowTurn) {
        if (ImGui::Button(TEXT::Localize("agent.approval.allowTurn").c_str()))
            resolve(0);
        ImGui::SameLine();
    }
    if (ImGui::Button(TEXT::Localize("agent.approval.allowOnce").c_str()))
        resolve(1);
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("agent.approval.deny").c_str()))
        resolve(2);
}

// 编辑器操作の承認待ちと、CLI 自身の道具（命令、文件编辑、浏览器など）の権限確認。
void DrawApprovals(EDITOR::AGENT::AgentHost& host, EDITOR::AGENT::AgentSession& session) {
    using HostDecision = EDITOR::AGENT::AgentHost::ApprovalDecision;
    using EDITOR::AGENT::AgentToolDecision;
    for (const EDITOR::AGENT::AgentHost::Approval& approval : host.PendingApprovals()) {
        ImGui::PushID(static_cast<int>(approval.id));
        ImGui::SeparatorText(TEXT::Localize("agent.approval.title").c_str());
        ImGui::TextWrapped("%s", approval.operation.c_str());
        ImGui::TextDisabled("%s", TEXT::Localize("agent.approval.permission." + approval.permission).c_str());
        if (ImGui::IsItemHovered() && !approval.parameters.empty())
            ImGui::SetTooltip("%s", approval.parameters.c_str());
        DrawDecisionButtons(!approval.approveEachTime, [&host, &approval](int choice) {
            host.ResolveApproval(approval.id, choice == 0   ? HostDecision::AllowTurn
                                              : choice == 1 ? HostDecision::AllowOnce
                                                            : HostDecision::Deny);
        });
        ImGui::PopID();
    }
    const std::vector<EDITOR::AGENT::AgentToolApproval> approvals = session.PendingApprovals();
    for (const EDITOR::AGENT::AgentToolApproval& approval : approvals) {
        ImGui::PushID(static_cast<int>(approval.id) + 0x40000);
        ImGui::SeparatorText(TEXT::Localize("agent.toolApproval.title").c_str());
        ImGui::TextWrapped("%s%s", approval.tool.c_str(),
                           approval.browser ? TEXT::Localize("agent.toolApproval.browser").c_str() : "");
        if (!approval.detail.empty())
            ImGui::TextDisabled("%s", approval.detail.c_str());
        if (!approval.warning.empty())
            ImGui::TextColored({1.0f, 0.62f, 0.3f, 1.0f}, "%s",
                               TEXT::Localize("agent.toolApproval.warning." + approval.warning).c_str());
        DrawDecisionButtons(!approval.grantKey.empty(), [&session, &approval](int choice) {
            session.ResolveApproval(approval.id, choice == 0   ? AgentToolDecision::AllowTurn
                                                 : choice == 1 ? AgentToolDecision::AllowOnce
                                                               : AgentToolDecision::Deny);
        });
        ImGui::PopID();
    }
}

// CLI が報告した用量だけを出す。報告が無い項目は出さない。
// 選べる模型。空欄は CLI の既定。一覧の外の名前も下の入力欄で書ける。切り替えは次の依頼から効く。
bool DrawModelChoice(EDITOR::AGENT::AgentPanelState& panel,
                     const std::vector<EDITOR::AGENT::AgentModelOption>& models) {
    bool changed = false;
    const auto found =
        std::find_if(models.begin(), models.end(), [&panel](const auto& option) { return option.id == panel.model; });
    std::string current = panel.model;
    if (panel.model[0] == '\0')
        current = TEXT::Localize("agent.model.default");
    else if (found != models.end() && !found->label.empty())
        current = found->label;
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##agentModel", current.c_str())) {
        if (ImGui::Selectable(TEXT::Localize("agent.model.default").c_str(), panel.model[0] == '\0')) {
            panel.model[0] = '\0';
            changed = true;
        }
        for (const EDITOR::AGENT::AgentModelOption& option : models) {
            const std::string text = (option.label.empty() ? option.id : option.label) + (option.isDefault ? " *" : "");
            if (ImGui::Selectable(text.c_str(), option.id == panel.model)) {
                std::snprintf(panel.model, sizeof(panel.model), "%s", option.id.c_str());
                changed = true;
            }
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(170.0f);
        if (ImGui::InputTextWithHint("##agentModelCustom", TEXT::Localize("agent.model").c_str(), panel.model,
                                     sizeof(panel.model)))
            changed = true;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TEXT::Localize("agent.model.nextTurn").c_str());
    return changed;
}

// 推理强度は選んだ模型が報告した段階から。模型が既定（空欄）なら既定の模型の段階。
bool DrawEffortChoice(EDITOR::AGENT::AgentPanelState& panel,
                      const std::vector<EDITOR::AGENT::AgentModelOption>& models) {
    bool changed = false;
    const auto model = std::find_if(models.begin(), models.end(), [&panel](const auto& option) {
        return panel.model[0] == '\0' ? option.isDefault : option.id == panel.model;
    });
    std::vector<std::string> efforts;
    if (model != models.end())
        efforts = model->efforts;
    else if (panel.kind == AgentKind::ClaudeCode && !models.empty())
        efforts = models.front().efforts;
    const std::string current =
        panel.effort[0] == '\0' ? TEXT::Localize("agent.effort.default") : std::string(panel.effort);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::BeginCombo("##agentEffort", current.c_str())) {
        if (ImGui::Selectable(TEXT::Localize("agent.effort.default").c_str(), panel.effort[0] == '\0')) {
            panel.effort[0] = '\0';
            changed = true;
        }
        for (const std::string& effort : efforts) {
            if (ImGui::Selectable(effort.c_str(), effort == panel.effort)) {
                std::snprintf(panel.effort, sizeof(panel.effort), "%s", effort.c_str());
                changed = true;
            }
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::InputTextWithHint("##agentEffortCustom", TEXT::Localize("agent.effort").c_str(), panel.effort,
                                     sizeof(panel.effort)))
            changed = true;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TEXT::Localize("agent.model.nextTurn").c_str());
    return changed;
}

// 上下文の占有。8 割を超えたら、新しい会話か圧縮を勧める。
void DrawContextBar(EDITOR::AGENT::AgentSession& session, std::string& error) {
    const EDITOR::AGENT::AgentUsage& usage = session.Usage();
    if (!session.Running() || usage.contextWindow == 0u)
        return;
    const float fraction =
        std::clamp(static_cast<float>(usage.contextTokens) / static_cast<float>(usage.contextWindow), 0.0f, 1.0f);
    const std::string overlay = TEXT::Localize("agent.usage.context") + " " + FormatTokens(usage.contextTokens) +
                                " / " + FormatTokens(usage.contextWindow);
    ImGui::ProgressBar(fraction, {-1.0f, 0.0f}, overlay.c_str());
    if (fraction < 0.8f)
        return;
    ImGui::TextColored({1.0f, 0.62f, 0.3f, 1.0f}, "%s", TEXT::Localize("agent.context.nearFull").c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(session.Busy());
    if (ImGui::SmallButton(TEXT::Localize("agent.context.compact").c_str()))
        (void)session.Compact(error);
    ImGui::EndDisabled();
}

void DrawUsage(const EDITOR::AGENT::AgentSession& session) {
    const EDITOR::AGENT::AgentUsage& usage = session.Usage();
    if (usage.windows.empty() && usage.contextWindow == 0u && usage.turnOutputTokens == 0u) {
        ImGui::TextDisabled("%s", TEXT::Localize(session.Kind() == AgentKind::ClaudeCode ? "agent.usage.afterFirst"
                                                                                         : "agent.usage.waiting")
                                      .c_str());
        return;
    }
    for (const EDITOR::AGENT::AgentUsage::Window& window : usage.windows) {
        const std::string label = TEXT::Localize("agent.usage.window." + window.name);
        const std::string reset = window.resetsAt > 0
                                      ? TEXT::Localize("agent.usage.resetsIn") + FormatRemaining(window.resetsAt)
                                      : std::string{};
        if (window.usedPercent >= 0) {
            ImGui::TextUnformatted(label.c_str());
            ImGui::SameLine();
            const std::string overlay =
                std::to_string(window.usedPercent) + "%" + (reset.empty() ? std::string{} : "  · " + reset);
            ImGui::ProgressBar(static_cast<float>(window.usedPercent) / 100.0f, {-1.0f, 0.0f}, overlay.c_str());
        } else if (!reset.empty()) {
            ImGui::TextDisabled("%s · %s", label.c_str(), reset.c_str());
        }
    }
    if (!usage.limitStatus.empty() && usage.limitStatus != "allowed")
        ImGui::TextColored({1.0f, 0.7f, 0.3f, 1.0f}, "%s",
                           TEXT::Localize("agent.usage.status." + usage.limitStatus).c_str());
    if (!usage.plan.empty())
        ImGui::TextDisabled("%s %s", TEXT::Localize("agent.usage.plan").c_str(), usage.plan.c_str());
    if (usage.contextWindow > 0u)
        ImGui::TextDisabled("%s %s / %s", TEXT::Localize("agent.usage.context").c_str(),
                            FormatTokens(usage.contextTokens).c_str(), FormatTokens(usage.contextWindow).c_str());
    if (usage.turnInputTokens > 0u || usage.turnOutputTokens > 0u)
        ImGui::TextDisabled("%s %s / %s", TEXT::Localize("agent.usage.turnTokens").c_str(),
                            FormatTokens(usage.turnInputTokens).c_str(), FormatTokens(usage.turnOutputTokens).c_str());
    if (usage.referenceCostUsd >= 0.0) {
        ImGui::TextDisabled("%s $%.2f", TEXT::Localize("agent.usage.referenceCost").c_str(), usage.referenceCostUsd);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TEXT::Localize("agent.usage.referenceCostHint").c_str());
    }
}
#endif

} // namespace

void DocumentSceneEditorController::TickAgentPanel() {
    EDITOR::AGENT::AgentPanelState& panel = agentPanel_;
    panel.session.Poll();
    if (!panel.turnActive || panel.session.Busy())
        return;
    panel.lastTurnUndoGroup = panel.turnUndoGroup;
    panel.turnUndoGroup = 0u;
    panel.turnActive = false;
    agentHost_.EndTurn();
    RememberConversation(panel);
}

void DocumentSceneEditorController::StartAgentTurn(DocumentSceneBase& scene) {
    EDITOR::AGENT::AgentPanelState& panel = agentPanel_;
    panel.error.clear();
    const std::string text = TEXT::TrimCopy(std::string(panel.prompt));
    if (text.empty() || panel.turnActive)
        return;
    if (!agentBridge_.IsRunning()) {
        panel.error = TEXT::Localize("agent.bridgeOff");
        return;
    }
    const auto& assets = scene.GetAssetDatabase();
    EDITOR::AGENT::AgentSessionRequest request{};
    request.kind = panel.kind;
    request.executable = panel.executables[AgentIndex(panel.kind)];
    request.projectRoot = assets.GetProjectRoot();
    request.hikariExecutable = EDITOR::AGENT::DetectAgentProjectFileInputs({}).hikariExecutable;
    request.allowProjectEdits = agentHost_.AccessMode() == EDITOR::AGENT::AgentAccessMode::DevelopProject;
    request.protectedRoots = engineRoots_;
    request.model = TEXT::TrimCopy(std::string(panel.model));
    request.effort = TEXT::TrimCopy(std::string(panel.effort));
    request.chrome = panel.chrome && panel.kind == AgentKind::ClaudeCode;
    request.userTools = panel.userTools;
    request.autoApprove = agentHost_.AutoApprove();
    // 設定が変わったら同じ会話を新しい設定で続ける。
    std::string resume = panel.resumeSession;
    if (panel.session.Running() && !SameSettings(panel.session.Request(), request)) {
        resume = panel.session.SessionId();
        panel.session.Stop();
    }
    if (!panel.session.Running()) {
        request.resumeSession = resume;
        request.runFolder = assets.GetLibraryRoot() / "EditorAI" / "AgentRuns" /
                            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        if (!panel.session.Start(request, panel.error))
            return;
        panel.resumeSession.clear();
    }
    EDITOR::AGENT::AgentMessage message{text};
    if (panel.includeEditorState) {
        std::string state = DescribeEditorState().data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (state.size() > kMaximumAttachedStateBytes)
            state = state.substr(0u, kMaximumAttachedStateBytes) + "...";
        message.text += "\n\n[HIKARI editor state when this request was sent]\n" + state;
    }
    if (!panel.session.Send(message, panel.error))
        return;
    panel.turnActive = true;
    panel.prompt[0] = '\0';
}

void DocumentSceneEditorController::RecordAgentOperation(const std::string& action,
                                                         const EDITOR::EditorOperationResult& result) {
    std::deque<EDITOR::AGENT::AgentPanelState::Operation>& operations = agentPanel_.operations;
    operations.push_front({action, result.message, result.ok});
    if (operations.size() > kMaximumAgentOperations)
        operations.pop_back();
    if (result.ok)
        viewportDropMessage_ =
            TEXT::Localize(agentHost_.AutoApprove() ? "agent.feedPrefixAuto" : "agent.feedPrefix") + action;
}

void DocumentSceneEditorController::DrawAgentPanel(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
    EDITOR::AGENT::AgentPanelState& panel = agentPanel_;
    const std::filesystem::path agentRuns = scene.GetAssetDatabase().GetLibraryRoot() / "EditorAI" / "AgentRuns";
    const auto checkLogin = [&panel, &agentRuns](AgentKind kind) {
        panel.loginChecks[AgentIndex(kind)].Start(
            kind, panel.executables[AgentIndex(kind)],
            agentRuns / ("login-" + std::string(kind == AgentKind::ClaudeCode ? "claude" : "codex") + ".txt"));
    };
    if (!panel.detected) {
        panel.executables = {EDITOR::AGENT::FindAgentExecutable(AgentKind::ClaudeCode),
                             EDITOR::AGENT::FindAgentExecutable(AgentKind::Codex)};
        for (const auto& entry : EDITOR::AGENT::kAgentKindNames)
            checkLogin(entry.value);
        panel.detected = true;
    }
    for (EDITOR::AGENT::AgentLoginCheck& check : panel.loginChecks)
        check.Poll();
    const std::filesystem::path conversations =
        scene.GetAssetDatabase().GetLibraryRoot() / "EditorAI" / "AgentConversations.json";
    if (panel.conversationsFile != conversations)
        LoadConversations(panel, conversations);
    if (const std::filesystem::path preferences =
            scene.GetAssetDatabase().GetLibraryRoot() / "EditorAI" / "AgentPanel.json";
        panel.preferencesFile != preferences)
        LoadPreferences(panel, preferences);
    // Codex が報告した模型一覧は次の会話の前から選べるよう残す。
    if (panel.kind == AgentKind::Codex && !panel.session.Models().empty() &&
        panel.session.Models() != panel.codexModels) {
        panel.codexModels = panel.session.Models();
        SavePreferences(panel);
    }
    const auto label = [&panel](AgentKind kind) {
        std::string text = TEXT::EnumToName(EDITOR::AGENT::kAgentKindNames, kind);
        if (panel.executables[AgentIndex(kind)].empty())
            text += " (" + TEXT::Localize("agent.notFound") + ")";
        return text;
    };
    ImGui::BeginDisabled(panel.turnActive);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("##agentKind", label(panel.kind).c_str())) {
        for (const auto& entry : EDITOR::AGENT::kAgentKindNames) {
            if (ImGui::Selectable(label(entry.value).c_str(), entry.value == panel.kind) && entry.value != panel.kind) {
                panel.kind = entry.value;
                panel.session.Stop();
                panel.resumeSession.clear();
                ApplyRemembered(panel);
                SavePreferences(panel);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("agent.redetect").c_str()))
        panel.detected = false;
    ImGui::EndDisabled();
    const std::filesystem::path& executable = panel.executables[AgentIndex(panel.kind)];
    ImGui::TextDisabled("%s", executable.empty() ? TEXT::Localize("agent.noAgent").c_str()
                                                 : TEXT::PathToUtf8(executable).c_str());
    // 未登录なら、终端で打つ命令を見せる（编辑器は利用者の資格情報を扱わない）。
    const EDITOR::AGENT::AgentLoginCheck& login = panel.loginChecks[AgentIndex(panel.kind)];
    if (login.GetState() == EDITOR::AGENT::AgentLoginCheck::State::LoggedOut) {
        const std::string command = login.LoginCommand();
        ImGui::TextColored({1.0f, 0.62f, 0.3f, 1.0f}, "%s", TEXT::Localize("agent.loginNeeded").c_str());
        ImGui::TextWrapped("%s", command.c_str());
        if (ImGui::SmallButton(TEXT::Localize("agent.copyCommand").c_str()))
            ImGui::SetClipboardText(command.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TEXT::Localize("agent.recheckLogin").c_str()))
            checkLogin(panel.kind);
    } else if (login.GetState() == EDITOR::AGENT::AgentLoginCheck::State::Failed) {
        // 調べられなかった時は未登录と決めつけず、CLI が言ったことをそのまま出す。
        ImGui::TextColored({1.0f, 0.62f, 0.3f, 1.0f}, "%s", TEXT::Localize("agent.loginCheckFailed").c_str());
        ImGui::TextWrapped("%s", login.Detail() == "timeout" ? TEXT::Localize("agent.loginCheckTimeout").c_str()
                                                             : login.Detail().c_str());
        if (ImGui::SmallButton(TEXT::Localize("agent.recheckLogin").c_str()))
            checkLogin(panel.kind);
    }
    if (!agentBridge_.IsRunning()) {
        ImGui::TextWrapped("%s", TEXT::Localize("agent.bridgeOff").c_str());
        if (ImGui::Button(TEXT::Localize("agent.enableBridge").c_str()))
            context_.aiIntegration.localBridgeEnabled = true;
    }
    // 访问模式は人だけが切り替える。切り替えると待っている要求と承認は取り消される。
    using EDITOR::AGENT::AgentAccessMode;
    const AgentAccessMode mode = agentHost_.AccessMode();
    ImGui::BeginDisabled(panel.turnActive);
    if (ImGui::RadioButton(TEXT::Localize("agent.mode.contentShort").c_str(), mode == AgentAccessMode::Content))
        agentHost_.SetAccessMode(AgentAccessMode::Content);
    ImGui::SameLine();
    if (ImGui::RadioButton(TEXT::Localize("agent.mode.developShort").c_str(), mode == AgentAccessMode::DevelopProject))
        agentHost_.SetAccessMode(AgentAccessMode::DevelopProject);
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "%s",
        TEXT::Localize(mode == AgentAccessMode::DevelopProject ? "agent.mode.develop" : "agent.mode.content").c_str());
    // 自動（無需批准）は人だけが入れる（どの操作からも切り替えられず、文件にも保存しない）。入れている間は目立たせる。
    bool autoApprove = agentHost_.AutoApprove();
    if (ImGui::Checkbox(TEXT::Localize("agent.autoApprove").c_str(), &autoApprove))
        agentHost_.SetAutoApprove(autoApprove);
    if (autoApprove)
        ImGui::TextColored({1.0f, 0.62f, 0.3f, 1.0f}, "%s", TEXT::Localize("agent.autoApproveOn").c_str());
    else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TEXT::Localize("agent.autoApproveHint").c_str());
    if (panel.session.Running() && ImGui::CollapsingHeader(TEXT::Localize("agent.usage.title").c_str()))
        DrawUsage(panel.session);
    ImGui::Separator();
    DrawApprovals(agentHost_, panel.session);

    ImGui::BeginChild("##agentTranscript", {0.0f, -190.0f}, false);
    if (panel.session.Events().empty())
        ImGui::TextDisabled("%s", TEXT::Localize("agent.emptyHint").c_str());
    for (const EDITOR::AGENT::AgentEvent& event : panel.session.Events())
        DrawAgentEvent(event);
    if (panel.turnActive)
        ImGui::TextDisabled("%s", TEXT::Localize("agent.running").c_str());
    if (panel.turnActive && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
        ImGui::SetScrollHereY(1.0f);
    if (!panel.operations.empty() && ImGui::CollapsingHeader(TEXT::Localize("agent.operations").c_str())) {
        for (const EDITOR::AGENT::AgentPanelState::Operation& operation : panel.operations)
            ImGui::TextDisabled("%s %s", operation.ok ? "·" : "×", operation.action.c_str());
    }
    ImGui::EndChild();

    if (!panel.error.empty())
        ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "%s", panel.error.c_str());
    DrawContextBar(panel.session, panel.error);
    ImGui::BeginDisabled(panel.turnActive);
    ImGui::InputTextMultiline("##agentPrompt", panel.prompt, sizeof(panel.prompt), {-1.0f, 60.0f});
    ImGui::Checkbox(TEXT::Localize("agent.includeState").c_str(), &panel.includeEditorState);
    if (panel.kind == AgentKind::ClaudeCode) {
        ImGui::SameLine();
        ImGui::Checkbox(TEXT::Localize("agent.chrome").c_str(), &panel.chrome);
    }
    ImGui::SameLine();
    ImGui::Checkbox(TEXT::Localize("agent.userTools").c_str(), &panel.userTools);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TEXT::Localize("agent.userToolsHint").c_str());
    // 模型と推理强度。CLI が報告した一覧（Codex）か別名（Claude Code）から選ぶ。自由に書くこともできる。
    const std::vector<EDITOR::AGENT::AgentModelOption> models =
        panel.kind == AgentKind::ClaudeCode ? EDITOR::AGENT::DefaultAgentModels(AgentKind::ClaudeCode)
        : !panel.session.Models().empty()   ? panel.session.Models()
                                            : panel.codexModels;
    bool choiceChanged = DrawModelChoice(panel, models);
    ImGui::SameLine();
    choiceChanged = DrawEffortChoice(panel, models) || choiceChanged;
    if (choiceChanged) {
        panel.remembered[AgentIndex(panel.kind)] = {TEXT::TrimCopy(std::string(panel.model)),
                                                    TEXT::TrimCopy(std::string(panel.effort))};
        SavePreferences(panel);
    }
    ImGui::BeginDisabled(executable.empty() || !agentBridge_.IsRunning());
    if (ImGui::Button(TEXT::Localize("agent.send").c_str()))
        StartAgentTurn(scene);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("agent.newConversation").c_str())) {
        panel.session.Stop();
        panel.resumeSession.clear();
        panel.lastTurnUndoGroup = 0u;
    }
    // 続ける会話を一覧から選ぶ。次の送信で起動する CLI がその会話を読み込む。
    std::vector<const EDITOR::AGENT::AgentPanelState::Conversation*> resumable;
    for (const auto& conversation : panel.conversations)
        if (conversation.kind == panel.kind)
            resumable.push_back(&conversation);
    if (!resumable.empty()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        const auto selected = std::find_if(resumable.begin(), resumable.end(),
                                           [&panel](const auto* entry) { return entry->id == panel.resumeSession; });
        const std::string preview = selected != resumable.end() ? (*selected)->title : TEXT::Localize("agent.recent");
        if (ImGui::BeginCombo("##agentRecent", preview.c_str())) {
            for (const auto* conversation : resumable) {
                ImGui::PushID(conversation->id.c_str());
                const std::string item = conversation->title.empty() ? conversation->id : conversation->title;
                if (ImGui::Selectable(item.c_str(), conversation->id == panel.resumeSession)) {
                    panel.session.Stop();
                    panel.resumeSession = conversation->id;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::EndDisabled();
    if (panel.turnActive && ImGui::Button(TEXT::Localize("agent.stop").c_str()))
        panel.session.Interrupt();
    // 一轮の記録の後や途中に人の編集があると、Undo 一回では一轮全部を戻せず人の編集も巻き込む。その時は押せなくして理由を出す。
    const size_t turnEntries = documentHistory_.CountUndoGroupEntries(panel.lastTurnUndoGroup);
    if (!panel.turnActive && turnEntries > 0u && !scene.IsRuntimePlayActive()) {
        const bool whole = documentHistory_.GetUndoGroup() == panel.lastTurnUndoGroup &&
                           documentHistory_.TopUndoGroupLength() == turnEntries;
        ImGui::BeginDisabled(!whole);
        if (ImGui::Button(TEXT::Localize("agent.undoTurn").c_str()))
            ExecuteSceneDocumentHistory(scene, false);
        ImGui::EndDisabled();
        if (!whole && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TEXT::Localize("agent.undoTurnBlocked").c_str());
    }
#else
    (void)scene;
#endif
}

} // namespace HIKARI
