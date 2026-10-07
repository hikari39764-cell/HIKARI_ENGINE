#include "HIKARI_AI_AssistantPanel.h"
#include "HIKARI_AI_MessageText.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include <map>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
void AiAssistantPanel::DrawReasoning(const std::string& text, bool streaming, bool incomplete) {
#if defined(HIKARI_WITH_EDITOR)
    if (!session_.SnapshotConfig().showReasoning)
        return;
    const auto key = streaming ? "ai.reasoning.streaming" : "ai.reasoning.title";
    ImGui::SetNextItemOpen(streaming, ImGuiCond_Once);
    if (!ImGui::TreeNode("##Reasoning", "%s", TEXT::Localize(key).c_str()))
        return;
    ImGui::TextWrapped("%s", TEXT::Localize("ai.reasoning.source").c_str());
    if (incomplete)
        ImGui::TextWrapped("%s", TEXT::Localize("ai.reasoning.incomplete").c_str());
    if (text.empty())
        ImGui::TextDisabled("%s", TEXT::Localize("ai.reasoning.unavailable").c_str());
    else {
        ImGui::BeginChild("##ReasoningText", {0, ImGui::GetTextLineHeightWithSpacing() * 9}, true);
        const bool follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 24.0f;
        ImGui::TextWrapped("%s", text.c_str());
        if (streaming && follow)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::TreePop();
#endif
}

void AiAssistantPanel::RevealDraft(const std::string& sessionId) {
    const auto result =
        session_.ExecuteRequest(nlohmann::json{{"action", "authoring.reveal"}, {"sessionId", sessionId}}.dump());
    if (!result.ok)
        attachmentError_ = result.message;
}

void AiAssistantPanel::DrawDraftMenu() {
#if defined(HIKARI_WITH_EDITOR)
    if (IconButton(EditorGlyph::Reveal, "Drafts", EditorButtonTone::Quiet, {28, 28},
                   TEXT::Localize("ai.task.drafts").c_str())) {
        const auto result = session_.ExecuteRequest(R"({"action":"authoring.sessions"})");
        draftSessions_ = result.ok ? result.data.value("sessions", nlohmann::json::array()) : nlohmann::json::array();
        if (!result.ok)
            attachmentError_ = result.message;
        else if (result.data.contains("recoveryErrors") && !result.data["recoveryErrors"].empty())
            attachmentError_ = result.data["recoveryErrors"].front().value("message", std::string{});
        ImGui::OpenPopup("##Drafts");
    }
    if (ImGui::BeginPopup("##Drafts")) {
        ImGui::TextDisabled("%s", TEXT::Localize("ai.task.drafts").c_str());
        if (draftSessions_.empty())
            ImGui::TextUnformatted(TEXT::Localize("ai.task.noDrafts").c_str());
        for (const auto& draft : draftSessions_) {
            const auto id = draft.value("sessionId", std::string{});
            const auto label = draft.value("name", draft.value("workspace", std::string{}));
            ImGui::PushID(id.c_str());
            if (ImGui::Selectable(label.c_str()))
                RevealDraft(id);
            bool hovered = ImGui::IsItemHovered();
            if (draft.value("recovered", false)) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", TEXT::Localize("ai.draft.recovered").c_str());
                hovered = hovered || ImGui::IsItemHovered();
            }
            if (hovered)
                ImGui::SetTooltip(
                    "%s\n%s", id.c_str(),
                    TEXT::Localize(draft.value("checkpointed", false) ? "ai.draft.protected" : "ai.draft.memoryOnly")
                        .c_str());
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
#endif
}

void AiAssistantPanel::DrawTask(const std::vector<AI_ASSISTANT::AiChatMessage>& messages, size_t begin, size_t end,
                                bool active) {
#if defined(HIKARI_WITH_EDITOR)
    const auto& palette = GetEditorThemePalette();
    size_t operations = 0, failures = 0, latestImage = end, latestProgress = end, latestTool = end;
    std::map<std::string, std::string> documents;
    std::string phase = "completed";
    for (size_t i = begin; i < end; ++i) {
        const auto& message = messages[i];
        if (message.role == "tool") {
            operations += message.data.value("archivedSteps", size_t{1});
            failures += message.data.value("archivedFailures", size_t{message.data.value("ok", true) ? 0u : 1u});
            latestTool = i;
            const auto id = message.data.value("sessionId", std::string{});
            if (!id.empty()) {
                auto& name = documents[id];
                if (message.data.contains("name"))
                    name = message.data.value("name", std::string{});
            }
        } else if (message.data.value("kind", std::string{}) == "progress") {
            if (!message.content.empty())
                latestProgress = i;
        } else
            phase = message.data.value("phase", std::string("completed"));
        if (!message.images.empty())
            latestImage = i;
    }
    const auto activity = session_.SnapshotActivity();
    if (active)
        phase = activity.phase;
    ImGui::Spacing();
    ImGui::TextColored(palette.accent, "%s", TEXT::Localize("ai.chat.assistant").c_str());
    if (active || operations || phase != "completed") {
        const auto key = "ai.phase." + phase;
        StatusBadge(TEXT::Localize(key).c_str(), (phase == "failed" || phase == "blocked" || phase == "needs_input")
                                                     ? EditorStatusTone::Warning
                                                     : EditorStatusTone::Normal);
        if (active) {
            const auto seconds =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - activity.since)
                    .count();
            ImGui::SameLine();
            ImGui::TextDisabled(TEXT::Localize("editor.ui.lld.s.cf7921").c_str(), static_cast<long long>(seconds));
            if (ImGui::IsItemHovered() && !activity.action.empty())
                ImGui::SetTooltip("%s", activity.action.c_str());
        }
    }
    if (latestProgress < end) {
        const auto text = AI_ASSISTANT::StripActionJsonForDisplay(messages[latestProgress].content);
        if (!text.empty())
            ImGui::TextWrapped("%s", text.c_str());
    }
    for (size_t i = begin; i < end; ++i) {
        if (messages[i].data.value("kind", std::string{}) != "progress")
            continue;
        ImGui::PushID(static_cast<int>(i));
        DrawReasoning(messages[i].data.value("_reasoningContent", std::string{}), false,
                      messages[i].data.value("incomplete", false));
        ImGui::PopID();
    }
    // ユーザーへの結論と進捗をログから分離する。
    for (size_t i = begin; i < end; ++i) {
        if (messages[i].role != "assistant" || messages[i].data.value("kind", std::string{}) == "progress")
            continue;
        const auto text = AI_ASSISTANT::StripActionJsonForDisplay(messages[i].content);
        if (!text.empty())
            ImGui::TextWrapped("%s", text.c_str());
    }
    if (latestTool < end && !messages[latestTool].data.value("ok", true) && active)
        ImGui::TextWrapped("%s", messages[latestTool].content.c_str());
    for (const auto& [id, name] : documents) {
        ImGui::PushID(id.c_str());
        if (!name.empty())
            ImGui::TextDisabled("%s", name.c_str());
        if (IconTextButton(EditorGlyph::Reveal, TEXT::Localize("ai.task.takeover").c_str(), "RevealDraft",
                           EditorButtonTone::Quiet))
            RevealDraft(id);
        ImGui::PopID();
    }
    if (latestImage < end)
        DrawAttachments(messages[latestImage].images);
    if (begin && messages[begin - 1].role == "user" && messages[begin - 1].data.contains("usage")) {
        const auto& usage = messages[begin - 1].data["usage"];
        const auto requests = usage.value("requests", int64_t{0});
        const auto inputReports = usage.value("inputReports", int64_t{0});
        const auto outputReports = usage.value("outputReports", int64_t{0});
        if (inputReports || outputReports)
            ImGui::TextDisabled(TEXT::Localize("ai.usage.reported").c_str(), static_cast<long long>(requests),
                                static_cast<long long>(usage.value("inputTokens", int64_t{0}) +
                                                       usage.value("outputTokens", int64_t{0})));
        else
            ImGui::TextDisabled(TEXT::Localize("ai.usage.unavailable").c_str(), static_cast<long long>(requests));
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            const auto field = [&usage, requests](const char* label, const char* value, const char* reports) {
                const auto count = usage.value(reports, int64_t{0});
                if (count)
                    ImGui::Text("%s: %lld (%lld/%lld)", TEXT::Localize(label).c_str(),
                                static_cast<long long>(usage.value(value, int64_t{0})), static_cast<long long>(count),
                                static_cast<long long>(requests));
                else
                    ImGui::Text("%s: —", TEXT::Localize(label).c_str());
            };
            field("ai.usage.input", "inputTokens", "inputReports");
            field("ai.usage.output", "outputTokens", "outputReports");
            field("ai.usage.cache", "cachedTokens", "cacheReports");
            ImGui::Text(TEXT::Localize("ai.usage.payload").c_str(), usage.value("lastSourceTextBytes", 0.0) / 1024,
                        usage.value("lastSentTextBytes", 0.0) / 1024, usage.value("lastSentImages", 0));
            ImGui::EndTooltip();
        }
    }
    if (operations) {
        const auto label = TEXT::Localize("ai.task.log");
        if (ImGui::TreeNode("##ExecutionLog", label.c_str(), static_cast<int>(operations),
                            static_cast<int>(failures))) {
            for (size_t i = begin; i < end; ++i) {
                const auto& message = messages[i];
                if (message.role != "tool")
                    continue;
                ImGui::PushID(static_cast<int>(i));
                if (!message.data.value("ok", true))
                    ImGui::TextColored(palette.accent, "!");
                const auto action = message.data.value("action", std::string("operation"));
                if (ImGui::TreeNode("##Operation", "%s", action.c_str())) {
                    ImGui::TextWrapped("%s", message.content.c_str());
                    if (message.data.contains("durationMs"))
                        ImGui::TextDisabled(TEXT::Localize("editor.ui.lld.ms.1fe6bb").c_str(),
                                            message.data["durationMs"].get<long long>());
                    DrawAttachments(message.images);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
#else
    (void)messages;
    (void)begin;
    (void)end;
    (void)active;
#endif
}
} // namespace HIKARI::EDITOR
