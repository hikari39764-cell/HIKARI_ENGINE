#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Editor/AI/HIKARI_AI_ChatClient.h"

namespace HIKARI::EDITOR {

    namespace AI_ASSISTANT {

        struct AiCapability {
            std::string id;
            std::string summary;
            std::string status; // stable | planned | unsupported
        };

        struct AiRequestResult {
            bool ok = false;
            std::string message;
            std::vector<std::string> details;
        };

        const std::vector<AiCapability>& QueryCapabilities();
        AiRequestResult SubmitAiRequest(std::string_view requestJson);

    } // namespace AI_ASSISTANT

    class AiAssistantPanel {
    public:
        AiAssistantPanel();
        ~AiAssistantPanel();

        void Draw(bool& open);

    private:
        void SendCurrentMessage();
        void RunConversation(const std::string& userText);
        void AppendMessage(const AI_ASSISTANT::AiChatMessage& message);
        std::string BuildSystemPrompt() const;
        void OpenApiSettingsBuffers();
        void SaveApiSettings();

        std::vector<AI_ASSISTANT::AiChatMessage> messages_{};
        std::mutex messageMutex_{};
        char inputBuffer_[4096] = {};
        char rawRequestBuffer_[2048] = {};
        bool showCapabilities_ = false;
        bool showRawRequest_ = false;
        bool showApiSettings_ = false;
        bool showApiKey_ = false;
        bool hasRawResult_ = false;
        size_t lastMessageCount_ = 0u;
        char apiKeyBuffer_[256] = {};
        char baseUrlBuffer_[512] = {};
        char modelBuffer_[128] = {};
        AI_ASSISTANT::AiRequestResult rawResult_{};
        AI_ASSISTANT::AiChatConfig config_{};
        bool configLoaded_ = false;
        std::atomic<bool> busy_{ false };
        std::thread worker_{};
    };

} // namespace HIKARI::EDITOR
