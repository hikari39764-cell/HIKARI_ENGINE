#pragma once
#include "Editor/AI/HIKARI_AI_AssistantSession.h"
namespace HIKARI::EDITOR {
class AiAssistantPanel {
public:
    explicit AiAssistantPanel(AiAssistantSession& session) : session_(session) {}
    ~AiAssistantPanel();
    void Draw(bool& open);
    void DrawSidebar(bool& open);
    void ResetComposer();
    void SetWorkspaceLabel(std::string label) { workspaceLabel_=std::move(label); }
private:
    void DrawContents();
    void DrawTask(const std::vector<AI_ASSISTANT::AiChatMessage>& messages, size_t begin, size_t end, bool active);
    void DrawDraftMenu();
    void RevealDraft(const std::string& sessionId);
    void DrawSettings();
    void DrawConversationMenu();
    void DrawComposer();
    void AddImage(const std::string& path);
    void PollAttachmentCapture();
    void DrawAttachments(const std::vector<AI_ASSISTANT::AiImageAttachment>& images);
    void SendCurrentMessage();
    void OpenApiSettingsBuffers();
    void SaveApiSettings();
    void OpenVisionBuffers();
    void SaveVisionSettings();
    void ResetVisionSettings();
    AiAssistantSession& session_;
    std::vector<AI_ASSISTANT::AiImageAttachment> attachments_{};
    char inputBuffer_[8192]{};
    char rawRequestBuffer_[2048]{};
    char apiKeyBuffer_[256]{};
    char baseUrlBuffer_[512]{};
    char modelBuffer_[128]{};
    char visionApiKeyBuffer_[256]{};
    char visionBaseUrlBuffer_[512]{};
    char visionModelBuffer_[128]{};
    bool showCapabilities_ = false;
    bool showRawRequest_ = false;
    bool showApiSettings_ = false;
    bool imageInputEnabled_ = true;
    int maxToolRounds_ = 40;
    int toolHistoryKiB_ = 24;
    bool requestUsage_ = true;
    bool showApiKey_ = false;
    bool showVisionKey_ = false;
    bool hasRawResult_ = false;
    float sidebarWidth_ = 380.0f;
    std::string attachmentError_{};
    std::string workspaceLabel_{};
    size_t lastMessageCount_ = 0;
    AI_ASSISTANT::AiRequestResult rawResult_{};
    nlohmann::json attachmentCapturePoll_;
    nlohmann::json draftSessions_ = nlohmann::json::array();
    struct ImagePreview { int handle = -1; unsigned width = 0, height = 0; int lastFrame = 0; };
    std::unordered_map<std::string, ImagePreview> imagePreviews_;
    std::vector<AI_ASSISTANT::AiConversationSummary> savedConversations_;
};
}
