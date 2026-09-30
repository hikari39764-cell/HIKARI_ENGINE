#include "HIKARI_AI_AssistantPanel.h"
#include "HIKARI_AI_MessageText.h"
#include "HIKARI_AI_ChatClient.h"
#include <cstdio>
#include <algorithm>
#include <string_view>
#include "Core/Text/HIKARI_Localization.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
void AiAssistantPanel::OpenApiSettingsBuffers() {
    const AI_ASSISTANT::AiChatConfig config = session_.SnapshotConfig();
    imageInputEnabled_ = config.imageInputEnabled;
    maxToolRounds_ = config.maxToolRounds;
    maxTaskTokens_ = config.maxTaskTokens;
    maxNoProgressRounds_ = config.maxNoProgressRounds;
    toolHistoryKiB_ = config.toolHistoryKiB;
    reasoningContentPolicy_ = config.reasoningContentPolicy;
    requestUsage_ = config.requestUsage;
    showReasoning_ = config.showReasoning;
    std::snprintf(apiKeyBuffer_, sizeof(apiKeyBuffer_), "%s", config.apiKey.c_str());
    std::snprintf(baseUrlBuffer_, sizeof(baseUrlBuffer_), "%s", config.baseUrl.c_str());
    std::snprintf(modelBuffer_, sizeof(modelBuffer_), "%s", config.model.c_str());
}

void AiAssistantPanel::SaveApiSettings() {
    AI_ASSISTANT::AiChatConfig updated{};
    updated.apiKey = AI_ASSISTANT::Trim(apiKeyBuffer_);
    updated.baseUrl = AI_ASSISTANT::Trim(baseUrlBuffer_);
    updated.model = AI_ASSISTANT::Trim(modelBuffer_);
    if (updated.baseUrl.empty()) {
        updated.baseUrl = "https://dashscope.aliyuncs.com/compatible-mode/v1";
    }
    if (updated.model.empty()) {
        updated.model = "qwen-plus";
    }
    updated.valid = !updated.apiKey.empty();
    updated.imageInputEnabled = imageInputEnabled_;
    updated.maxToolRounds = maxToolRounds_;
    updated.maxTaskTokens = (std::max)(0, maxTaskTokens_);
    updated.maxNoProgressRounds = std::clamp(maxNoProgressRounds_, 0, AI_ASSISTANT::kMaxAiToolRounds);
    updated.toolHistoryKiB = toolHistoryKiB_;
    updated.reasoningContentPolicy = reasoningContentPolicy_;
    updated.requestUsage = requestUsage_;
    updated.showReasoning = showReasoning_;
    if (!AI_ASSISTANT::SaveAiChatConfig(updated)) {
        attachmentError_ = "无法保存 AI 设置。";
        return;
    }
    {
        std::lock_guard<std::mutex> lock(session_.stateMutex_);
        session_.configLoaded_ = AI_ASSISTANT::LoadAiChatConfig(session_.config_);
    }
    OpenApiSettingsBuffers();
}

void AiAssistantPanel::DrawSettings() {
#if defined(HIKARI_WITH_EDITOR)
    if (showCapabilities_) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader(TEXT::Localize("ai.capabilities").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("##AiCapabilities", 3,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(TEXT::Localize("ai.capabilities.status").c_str());
                ImGui::TableSetupColumn(TEXT::Localize("ai.capabilities.id").c_str());
                ImGui::TableSetupColumn(TEXT::Localize("ai.capabilities.summary").c_str());
                ImGui::TableHeadersRow();
                for (const AI_ASSISTANT::AiCapability& capability : session_.Capabilities()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", capability.status.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(capability.id.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", capability.summary.c_str());
                }
                ImGui::EndTable();
            }
        }
    }

    if (showApiSettings_) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader(TEXT::Localize("ai.menu.apiSettings").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputText(TEXT::Localize("ai.api.key").c_str(), apiKeyBuffer_, sizeof(apiKeyBuffer_),
                             showApiKey_ ? 0 : ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            ImGui::Checkbox(TEXT::Localize("ai.api.show").c_str(), &showApiKey_);
            ImGui::InputText(TEXT::Localize("ai.api.baseUrl").c_str(), baseUrlBuffer_, sizeof(baseUrlBuffer_));
            ImGui::InputText(TEXT::Localize("ai.api.model").c_str(), modelBuffer_, sizeof(modelBuffer_));
            ImGui::Checkbox(TEXT::Localize("ai.api.imageInput").c_str(), &imageInputEnabled_);
            ImGui::InputInt(TEXT::Localize("ai.api.toolRounds").c_str(), &maxToolRounds_);
            maxToolRounds_ = std::clamp(maxToolRounds_, 1, AI_ASSISTANT::kMaxAiToolRounds);
            ImGui::InputInt(TEXT::Localize("ai.api.tokenLimit").c_str(), &maxTaskTokens_, 10000, 100000);
            maxTaskTokens_ = (std::max)(0, maxTaskTokens_);
            ImGui::InputInt(TEXT::Localize("ai.api.noProgressLimit").c_str(), &maxNoProgressRounds_);
            maxNoProgressRounds_ = std::clamp(maxNoProgressRounds_, 0, AI_ASSISTANT::kMaxAiToolRounds);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TEXT::Localize("ai.api.tokenLimitHint").c_str());
            ImGui::SliderInt(TEXT::Localize("ai.api.historyBudget").c_str(), &toolHistoryKiB_, 8, 256, "%d KiB");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TEXT::Localize("ai.api.historyBudgetHint").c_str());
            ImGui::Checkbox(TEXT::LocalizeCStr("ai.reasoning.show"), &showReasoning_);
            const auto reasoningChoices = TEXT::EnumNameList(AI_ASSISTANT::kAiReasoningContentPolicyNames);
            const std::string selectedPolicy =
                TEXT::EnumToName(AI_ASSISTANT::kAiReasoningContentPolicyNames, reasoningContentPolicy_, "preserve");
            const std::string selectedPolicyLabel = TEXT::Localize(("ai.api.reasoning." + selectedPolicy).c_str());
            if (ImGui::BeginCombo(TEXT::Localize("ai.api.reasoning").c_str(), selectedPolicyLabel.c_str())) {
                for (const std::string_view choice : reasoningChoices) {
                    const auto value = TEXT::ParseEnum(AI_ASSISTANT::kAiReasoningContentPolicyNames, choice,
                                                       AI_ASSISTANT::AiReasoningContentPolicy::Preserve);
                    const std::string optionKey = "ai.api.reasoning." + std::string(choice);
                    const bool selected = value == reasoningContentPolicy_;
                    if (ImGui::Selectable(TEXT::Localize(optionKey.c_str()).c_str(), selected))
                        reasoningContentPolicy_ = value;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TEXT::Localize("ai.api.reasoning.hint").c_str());
            ImGui::Checkbox(TEXT::Localize("ai.api.usage").c_str(), &requestUsage_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TEXT::Localize("ai.api.usageHint").c_str());
            if (ImGui::Button(TEXT::Localize("ai.api.save").c_str())) {
                SaveApiSettings();
            }
            ImGui::SameLine();
            if (ImGui::Button(TEXT::Localize("ai.api.reset").c_str())) {
                AI_ASSISTANT::ResetAiChatConfig();
                {
                    std::lock_guard<std::mutex> lock(session_.stateMutex_);
                    session_.configLoaded_ = AI_ASSISTANT::LoadAiChatConfig(session_.config_);
                }
                OpenApiSettingsBuffers();
            }
            ImGui::TextDisabled(TEXT::Localize("ai.api.savedTo").c_str());
        }
    }

    if (showRawRequest_) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader(TEXT::Localize("ai.raw.title").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputTextMultiline("##AiRawRequest", rawRequestBuffer_, sizeof(rawRequestBuffer_),
                                      ImVec2(-1.0f, 64.0f));
            if (ImGui::Button(TEXT::Localize("ai.raw.run").c_str())) {
                rawResult_ = session_.ExecuteRequest(rawRequestBuffer_);
                hasRawResult_ = true;
            }
            if (hasRawResult_) {
                ImGui::TextColored(rawResult_.ok ? ImVec4{0.28f, 0.82f, 0.48f, 1.0f} : ImVec4{1.0f, 0.42f, 0.35f, 1.0f},
                                   rawResult_.ok ? TEXT::Localize("ai.raw.ok").c_str()
                                                 : TEXT::Localize("ai.raw.fail").c_str());
                ImGui::TextWrapped("%s", rawResult_.message.c_str());
                for (const std::string& detail : rawResult_.details) {
                    ImGui::BulletText("%s", detail.c_str());
                }
            }
        }
    }

#endif
}
} // namespace HIKARI::EDITOR
