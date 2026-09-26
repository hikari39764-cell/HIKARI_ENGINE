#include "HIKARI_AI_AssistantPanel.h"
#include "HIKARI_AI_MessageText.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Render2D/HIKARI_DxTexture.h"
#include <algorithm>
#include <filesystem>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {
void AiAssistantPanel::TickAttachments() { PollAttachmentCapture(); }
void AiAssistantPanel::DrawEmbedded() { DrawContents(); }
void AiAssistantPanel::DrawPreferences() {
    if (!showApiSettings_) {
        OpenApiSettingsBuffers();
        showApiSettings_ = true;
    }
    DrawSettings();
}
AiAssistantPanel::~AiAssistantPanel() {
    ResetComposer();
    for (auto& [path, preview] : imagePreviews_)
        if (preview.handle >= 0)
            DXTEX::DxTextureManager::ReleaseTextureDeferred(preview.handle);
}

void AiAssistantPanel::DrawAttachments(const std::vector<AI_ASSISTANT::AiImageAttachment>& images) {
#if defined(HIKARI_WITH_EDITOR)
    for (size_t index = 0; index < images.size(); ++index) {
        const auto& image = images[index];
        ImGui::PushID(static_cast<int>(index));
        auto& preview = imagePreviews_[image.path];
        const float available = (std::min)(ImGui::GetContentRegionAvail().x, 320.0f);
        float ratio =
            preview.width && preview.height ? (std::min)(available / preview.width, 190.0f / preview.height) : 1.0f;
        const ImVec2 footprint = preview.width && preview.height ? ImVec2(preview.width * ratio, preview.height * ratio)
                                                                 : ImVec2(available, 160);
        if (!ImGui::IsRectVisible(footprint)) {
            ImGui::Dummy(footprint);
            ImGui::PopID();
            continue;
        }
        preview.lastFrame = ImGui::GetFrameCount();
        if (preview.handle < 0)
            preview.handle = DXTEX::DxTextureManager::LoadTextureLinear("ai/" + image.path, image.path);
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
        if (preview.handle >= 0) {
            srv = DXTEX::DxTextureManager::GetSrvGpuHandle(preview.handle);
            DXTEX::DxTextureManager::GetTextureSize(preview.handle, preview.width, preview.height);
        }
        const auto w = preview.width, h = preview.height;
        if (srv.ptr && w && h) {
            const ImTextureID texture = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr));
            ratio = (std::min)(available / w, 190.0f / h);
            const float width = w * ratio, height = h * ratio;
            if (ImGui::ImageButton("Image", texture, {width, height}))
                ImGui::OpenPopup("##FullImage");
            if (ImGui::BeginPopup("##FullImage")) {
                const auto area = ImGui::GetMainViewport()->WorkSize;
                const float fullRatio = (std::min)(area.x * 0.75f / w, area.y * 0.75f / h);
                ImGui::Image(texture, {w * fullRatio, h * fullRatio});
                ImGui::EndPopup();
            }
        } else
            ImGui::TextDisabled("%s", TEXT::Localize("ai.image.unavailable").c_str());
        if (!image.label.empty())
            ImGui::TextDisabled("%s", image.label.c_str());
        ImGui::PopID();
    }
#else
    (void)images;
#endif
}

void AiAssistantPanel::DrawApprovals() {
#if defined(HIKARI_WITH_EDITOR)
    // 承認待ちは会話より先に出す。承認するまで AI の処理は止まっている。
    for (const AiAssistantSession::ApprovalRequest& approval : session_.PendingApprovals()) {
        ImGui::PushID(static_cast<int>(approval.id));
        StatusBadge(TEXT::Localize("ai.approval.title").c_str(), EditorStatusTone::Warning);
        ImGui::SameLine();
        ImGui::TextUnformatted(approval.action.c_str());
        ImGui::TextWrapped("%s", TEXT::Localize("ai.approval.permission." + approval.permission).c_str());
        ImGui::TextDisabled("%s", approval.parameters.c_str());
        if (ImGui::Button(TEXT::Localize("ai.approval.approve").c_str())) {
            session_.ResolveApproval(approval.id, true);
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::Localize("ai.approval.reject").c_str())) {
            session_.ResolveApproval(approval.id, false);
        }
        ImGui::Separator();
        ImGui::PopID();
    }
#endif
}

void AiAssistantPanel::DrawContents() {
#if defined(HIKARI_WITH_EDITOR)
    const auto& palette = GetEditorThemePalette();
    const bool busy = session_.IsBusy();
    const auto config = session_.SnapshotConfig();
    const auto activity = session_.SnapshotActivity();
    const bool attention = activity.phase == "failed" || activity.phase == "blocked" ||
                           activity.phase == "needs_input" || activity.phase == "stopped" ||
                           activity.phase == "paused" || activity.phase == "budget";
    const auto statusKey = busy            ? "ai.phase." + activity.phase
                           : !config.valid ? std::string("ai.status.noApi")
                           : attention     ? "ai.phase." + activity.phase
                                           : std::string("ai.status.ready");
    StatusBadge(TEXT::Localize(statusKey).c_str(), (!config.valid || attention) ? EditorStatusTone::Warning
                                                   : busy                       ? EditorStatusTone::Normal
                                                                                : EditorStatusTone::Ready);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", config.model.c_str());
    if (!workspaceLabel_.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", workspaceLabel_.c_str());
    }
    DrawConversationMenu();
    ImGui::SameLine();
    DrawDraftMenu();
    ImGui::Spacing();
    ImGui::Separator();
    DrawApprovals();
    const float inputHeight =
        126.0f + (!attachments_.empty() ? 28.0f : 0.0f) + (!attachmentError_.empty() ? 42.0f : 0.0f);
    ImGui::BeginChild("##Conversation", {0, -inputHeight}, false);
    std::vector<AI_ASSISTANT::AiChatMessage> messages;
    {
        std::lock_guard<std::mutex> lock(session_.messageMutex_);
        messages = session_.messages_;
    }
    const bool follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 32.0f;
    if (messages.empty()) {
        ImGui::Dummy({0, 24});
        PanelTitle(TEXT::Localize("ai.welcome.title").c_str());
        ImGui::TextWrapped("%s", TEXT::Localize("ai.welcome.detail").c_str());
    }
    for (size_t i = 0; i < messages.size();) {
        ImGui::PushID(static_cast<int>(i));
        if (messages[i].role == "user") {
            ImGui::Spacing();
            ImGui::TextColored(palette.textMuted, "%s", TEXT::Localize("ai.chat.you").c_str());
            ImGui::TextWrapped("%s", messages[i].content.c_str());
            DrawAttachments(messages[i].images);
            ++i;
        }
        size_t end = i;
        while (end < messages.size() && messages[end].role != "user")
            ++end;
        if (end > i || (busy && end == messages.size()))
            DrawTask(messages, i, end, busy && end == messages.size());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PopID();
        i = end;
    }
    std::string partial;
    {
        std::lock_guard<std::mutex> lock(session_.streamMutex_);
        if (session_.streamingActive_)
            partial = AI_ASSISTANT::StripActionJsonForDisplay(session_.streamingPartial_);
    }
    if (busy && !partial.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(palette.accent, "%s", TEXT::Localize("ai.chat.assistant").c_str());
        ImGui::TextWrapped("%s", partial.c_str());
    }
    if (follow && (messages.size() != lastMessageCount_ || busy))
        ImGui::SetScrollHereY(1.0f);
    lastMessageCount_ = messages.size();
    ImGui::EndChild();
    DrawComposer();
    size_t liveImages = 0;
    for (auto& [path, preview] : imagePreviews_) {
        if (preview.handle < 0)
            continue;
        ++liveImages;
        if (preview.lastFrame < ImGui::GetFrameCount() - 120 ||
            (liveImages > 24 && preview.lastFrame < ImGui::GetFrameCount())) {
            DXTEX::DxTextureManager::ReleaseTextureDeferred(preview.handle);
            preview.handle = -1;
        }
    }
    if (!session_.storageError_.empty())
        ImGui::TextWrapped("%s", session_.storageError_.c_str());

#endif
}

void AiAssistantPanel::SendCurrentMessage() {
    if (session_.SubmitMessage(inputBuffer_, attachments_)) {
        inputBuffer_[0] = '\0';
        attachments_.clear();
        attachmentError_.clear();
    }
}
} // namespace HIKARI::EDITOR
