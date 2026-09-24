#include "Core/Text/HIKARI_UtfPath.h"
#include "HIKARI_AI_AssistantPanel.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Editor/Platform/HIKARI_EditorFileDialog.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Core/Text/HIKARI_Localization.h"
#include <filesystem>
#include <algorithm>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
void AiAssistantPanel::ResetComposer() {
    if (attachmentCapturePoll_.is_object() && attachmentCapturePoll_.contains("captureId"))
        RENDER3D::EDITORVIEW::CancelViewCapture(attachmentCapturePoll_["captureId"].get<uint64_t>());
    attachmentCapturePoll_.clear();
    attachments_.clear();
    inputBuffer_[0] = '\0';
    attachmentError_.clear();
}
void AiAssistantPanel::AddImage(const std::string& path) {
    if (attachments_.size() >= 6) {
        attachmentError_ = TEXT::Localize("ai.image.limit");
        return;
    }
    std::string uri;
    if (!AI_ASSISTANT::ReadAiImageDataUri(path, uri, attachmentError_))
        return;
    const std::filesystem::path source(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    const auto directory = session_.conversationFolder_.parent_path() / "Images";
    if (session_.conversationFolder_.empty()) {
        attachmentError_ = TEXT::Localize("ai.image.noProject");
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const auto destination = directory / (GenerateAssetGuid().value + source.extension().string());
    if (!error)
        std::filesystem::copy_file(source, destination, error);
    if (error) {
        attachmentError_ = error.message();
        return;
    }
    attachments_.push_back({TEXT::PathToUtf8(destination), TEXT::PathToUtf8(source.filename())});
    attachmentError_.clear();
}
void AiAssistantPanel::PollAttachmentCapture() {
    if (attachmentCapturePoll_.empty())
        return;
    const auto result = session_.ExecuteRequest(attachmentCapturePoll_.dump());
    if (result.ok && result.data.value("pending", false))
        return;
    attachmentCapturePoll_.clear();
    if (!result.ok) {
        attachmentError_ = result.message;
        return;
    }
    if (result.data.contains("images"))
        for (const auto& image : result.data["images"])
            AddImage(image.at("path"));
}
void AiAssistantPanel::DrawConversationMenu() {
#if defined(HIKARI_WITH_EDITOR)
    ImGui::BeginDisabled(session_.IsBusy());
    if (IconButton(EditorGlyph::NewDocument, "NewConversation", EditorButtonTone::Quiet, {26, 26},
                   TEXT::Localize("ai.conversation.new").c_str())) {
        if (session_.NewConversation())
            ResetComposer();
    }
    ImGui::SameLine();
    if (IconButton(EditorGlyph::Library, "Conversations", EditorButtonTone::Quiet, {26, 26},
                   TEXT::Localize("ai.conversation.history").c_str())) {
        savedConversations_ = session_.Conversations();
        ImGui::OpenPopup("##Conversations");
    }
    if (ImGui::BeginPopup("##Conversations")) {
        ImGui::TextDisabled("%s", TEXT::Localize("ai.conversation.history").c_str());
        for (const auto& item : savedConversations_) {
            ImGui::PushID(item.id.c_str());
            if (ImGui::Selectable(item.title.c_str(), item.id == session_.conversationId_)) {
                if (session_.SelectConversation(item.id))
                    ResetComposer();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
#endif
}
void AiAssistantPanel::DrawComposer() {
#if defined(HIKARI_WITH_EDITOR)
    const bool busy = session_.IsBusy();
    if (!attachments_.empty()) {
        ImGui::TextDisabled(TEXT::Localize("ai.image.count").c_str(), static_cast<int>(attachments_.size()));
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Close, "ClearAttachments", EditorButtonTone::Quiet, {22, 22},
                       TEXT::Localize("ai.image.clear").c_str()))
            attachments_.clear();
    }
    if (!attachmentError_.empty())
        ImGui::TextWrapped("%s", attachmentError_.c_str());
    ImGui::BeginDisabled(busy);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##Message", inputBuffer_, sizeof(inputBuffer_), {-1, 76},
                              ImGuiInputTextFlags_CtrlEnterForNewLine);
    // InputTextMultiline の戻り値は編集の有無。Enter の確定だけを送信する。
    const bool submitted =
        ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().KeyCtrl;
    if (IconButton(EditorGlyph::Texture, "AttachImage", EditorButtonTone::Quiet, {28, 28},
                   TEXT::Localize("ai.image.attach").c_str())) {
        if (const auto path = FILE_DIALOG::PickFile({}, L"Images", L"*.png;*.jpg;*.jpeg;*.gif;*.webp"))
            AddImage(TEXT::PathToUtf8(*path));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!attachmentCapturePoll_.empty());
    if (IconButton(EditorGlyph::Camera, "AttachPreview", EditorButtonTone::Quiet, {28, 28},
                   TEXT::Localize("ai.image.capture").c_str())) {
        const auto result = session_.ExecuteRequest(R"({"action":"capture.preview"})");
        if (result.ok && result.data.contains("poll"))
            attachmentCapturePoll_ = result.data["poll"];
        else
            attachmentError_ = result.message;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Enter");
    ImGui::SameLine((std::max)(0.0f, ImGui::GetWindowContentRegionMax().x - 64.0f));
    if (!busy && session_.CanResume() && inputBuffer_[0] == '\0' && attachments_.empty()) {
        if (IconTextButton(EditorGlyph::Play, TEXT::Localize("ai.task.resume").c_str(), "ResumeTask",
                           EditorButtonTone::Primary))
            session_.Resume();
    } else if (busy) {
        if (IconTextButton(EditorGlyph::Stop, TEXT::Localize("ai.task.stop").c_str(), "StopTask",
                           EditorButtonTone::Quiet))
            session_.Cancel();
    } else if (IconTextButton(EditorGlyph::Send, TEXT::Localize("ai.send").c_str(), "SendMessage",
                              EditorButtonTone::Primary) ||
               submitted)
        SendCurrentMessage();
#endif
}
} // namespace HIKARI::EDITOR
