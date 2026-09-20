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
void AiAssistantPanel::DrawSidebar(bool& open) {
#if defined(HIKARI_WITH_EDITOR)
    PollAttachmentCapture();
    const auto& palette = GetEditorThemePalette();
    auto* viewport = ImGui::GetMainViewport();
    const float scale = ImGui::GetFontSize() / 16.0f;
    const float rail = 44.0f * scale;
    const float width = open ? std::clamp(sidebarWidth_ * scale,
        280.0f * scale, (std::max)(280.0f * scale, viewport->Size.x * 0.48f)) : rail;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, palette.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        open ? ImVec2(16, 14) : ImVec2(6, 14));
    const bool visible = ImGui::BeginViewportSideBar("###AI Assistant", viewport,
        ImGuiDir_Right, width, ImGuiWindowFlags_NoSavedSettings);
    if (visible) {
        if (!open) {
            if (IconButton(EditorGlyph::Assistant, "ExpandAssistant", EditorButtonTone::Quiet,
                {32 * scale, 32 * scale}, TEXT::Localize("ai.sidebar.expand").c_str())) open = true;
            if (session_.IsBusy()) {
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled({pos.x + 16 * scale, pos.y + 10}, 3.0f, ImGui::GetColorU32(palette.accent));
            }
        } else {
            const ImVec2 origin = ImGui::GetWindowPos();
            const ImRect resizeBounds(origin,{origin.x+5.0f,origin.y+ImGui::GetWindowHeight()});
            const ImGuiID resizeId=ImGui::GetID("##SidebarResize");
            bool resizeHovered=false,resizeHeld=false;
            ImGui::ItemAdd(resizeBounds,resizeId);
            ImGui::ButtonBehavior(resizeBounds,resizeId,&resizeHovered,&resizeHeld);
            if (resizeHovered || resizeHeld) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (resizeHeld) sidebarWidth_ = std::clamp(sidebarWidth_ - ImGui::GetIO().MouseDelta.x / scale, 280.0f, 640.0f);
            ImGui::TextColored(palette.accent, "HIKARI");
            ImGui::SameLine();
            ImGui::TextUnformatted(TEXT::Localize("ai.title").c_str());
            ImGui::SameLine((std::max)(0.0f, ImGui::GetWindowContentRegionMax().x - 68.0f));
            if (IconButton(EditorGlyph::Settings, "AssistantSettings", EditorButtonTone::Quiet, {28,28}, TEXT::Localize("ai.menu.apiSettings").c_str())) {
                OpenApiSettingsBuffers(); OpenVisionBuffers();
                showApiSettings_ = true;
                ImGui::OpenPopup("##AssistantSettings");
            }
            ImGui::SameLine(0, 4);
            if (IconButton(EditorGlyph::CollapseRight, "CollapseAssistant", EditorButtonTone::Quiet, {28,28}, TEXT::Localize("ai.sidebar.collapse").c_str())) open = false;
            ImGui::Spacing();
            DrawContents();
            ImGui::SetNextWindowSize({520, 580}, ImGuiCond_Appearing);
            if (ImGui::BeginPopup("##AssistantSettings")) {
                DrawSettings();
                ImGui::Separator();
                ImGui::Checkbox(TEXT::Localize("ai.menu.capabilities").c_str(), &showCapabilities_);
                ImGui::Checkbox(TEXT::Localize("ai.menu.rawJson").c_str(), &showRawRequest_);
                ImGui::EndPopup();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
#else
    (void)open;
#endif
}

AiAssistantPanel::~AiAssistantPanel() {
    ResetComposer();
    for (auto& [path,preview]:imagePreviews_) if (preview.handle>=0) DXTEX::DxTextureManager::ReleaseTextureDeferred(preview.handle);
}

void AiAssistantPanel::Draw(bool& open) { DrawSidebar(open); }

void AiAssistantPanel::DrawAttachments(const std::vector<AI_ASSISTANT::AiImageAttachment>& images) {
#if defined(HIKARI_WITH_EDITOR)
    for (size_t index = 0; index < images.size(); ++index) {
        const auto& image = images[index];
        ImGui::PushID(static_cast<int>(index));
        auto& preview=imagePreviews_[image.path];
        const float available=(std::min)(ImGui::GetContentRegionAvail().x,320.0f);
        float ratio=preview.width && preview.height ? (std::min)(available/preview.width,190.0f/preview.height) : 1.0f;
        const ImVec2 footprint=preview.width && preview.height ? ImVec2(preview.width*ratio,preview.height*ratio) : ImVec2(available,160);
        if (!ImGui::IsRectVisible(footprint)) { ImGui::Dummy(footprint); ImGui::PopID(); continue; }
        preview.lastFrame=ImGui::GetFrameCount();
        if (preview.handle<0) preview.handle=DXTEX::DxTextureManager::LoadTextureLinear("ai/"+image.path,image.path);
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
        if (preview.handle>=0) {
            srv=DXTEX::DxTextureManager::GetSrvGpuHandle(preview.handle);
            DXTEX::DxTextureManager::GetTextureSize(preview.handle,preview.width,preview.height);
        }
        const auto w=preview.width,h=preview.height;
        if (srv.ptr && w && h) {
            const ImTextureID texture = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr));
            ratio=(std::min)(available/w,190.0f/h);
            const float width=w*ratio,height=h*ratio;
            if (ImGui::ImageButton("Image", texture, {width, height})) ImGui::OpenPopup("##FullImage");
            if (ImGui::BeginPopup("##FullImage")) {
                const auto area = ImGui::GetMainViewport()->WorkSize;
                const float fullRatio = (std::min)(area.x * 0.75f / w, area.y * 0.75f / h);
                ImGui::Image(texture, {w * fullRatio, h * fullRatio});
                ImGui::EndPopup();
            }
        } else ImGui::TextDisabled("%s", TEXT::Localize("ai.image.unavailable").c_str());
        if (!image.label.empty()) ImGui::TextDisabled("%s", image.label.c_str());
        ImGui::PopID();
    }
#else
    (void)images;
#endif
}

void AiAssistantPanel::DrawContents() {
#if defined(HIKARI_WITH_EDITOR)
    const auto& palette = GetEditorThemePalette();
    const bool busy = session_.IsBusy();
    const auto config = session_.SnapshotConfig();
    StatusBadge(TEXT::Localize(busy ? "ai.status.processing" : config.valid ? "ai.status.ready" : "ai.status.noApi").c_str(),
        !config.valid ? EditorStatusTone::Warning : busy ? EditorStatusTone::Normal : EditorStatusTone::Ready);
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
    const float inputHeight = 126.0f + (!attachments_.empty() ? 28.0f : 0.0f) + (!attachmentError_.empty() ? 42.0f : 0.0f);
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
        while (end < messages.size() && messages[end].role != "user") ++end;
        if (end > i || (busy && end == messages.size())) DrawTask(messages, i, end, busy && end == messages.size());
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
    if (follow && (messages.size() != lastMessageCount_ || busy)) ImGui::SetScrollHereY(1.0f);
    lastMessageCount_ = messages.size();
    ImGui::EndChild();
    DrawComposer();
    size_t liveImages=0;
    for (auto& [path,preview]:imagePreviews_) {
        if (preview.handle<0) continue;
        ++liveImages;
        if (preview.lastFrame < ImGui::GetFrameCount()-120 || (liveImages>24 && preview.lastFrame<ImGui::GetFrameCount())) {
            DXTEX::DxTextureManager::ReleaseTextureDeferred(preview.handle); preview.handle=-1;
        }
    }
    if (!session_.storageError_.empty()) ImGui::TextWrapped("%s",session_.storageError_.c_str());

#endif
}

void AiAssistantPanel::SendCurrentMessage() {
    if (session_.SubmitMessage(inputBuffer_, attachments_)) {
        inputBuffer_[0] = '\0';
        attachments_.clear();
        attachmentError_.clear();
    }
}
}
