#pragma once

#include <atomic>
#include <functional>
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
            // Stable machine-readable error code for failures (for example
            // NODE_NOT_FOUND / PIN_TYPE_MISMATCH / COMPILE_FAILED).
            std::string code{};
            std::string message;
            std::vector<std::string> details;
            // Actionable suggestions for the model to self-correct.
            std::vector<std::string> hints;
        };

        const std::vector<AiCapability>& QueryCapabilities();
        AiRequestResult SubmitAiRequest(std::string_view requestJson);

    } // namespace AI_ASSISTANT

    class AiAssistantPanel {
    public:
        using AiSceneActionCallback =
            std::function<AI_ASSISTANT::AiRequestResult(
                std::string_view)>;
        using AiContextCallback = std::function<std::string()>;

        AiAssistantPanel();
        ~AiAssistantPanel();

        void Draw(bool& open);
        void SetSceneActionCallback(AiSceneActionCallback callback);
        void SetContextCallback(AiContextCallback callback);
        void SetBridgeStatus(const std::string& status);
        AI_ASSISTANT::AiRequestResult ExecuteRequest(
            std::string_view json);

    private:
        void SendCurrentMessage();
        void RunConversation(const std::string& userText);
        void AppendMessage(const AI_ASSISTANT::AiChatMessage& message);
        void AppendReasoning(const std::string& text);
        std::string BuildSystemPrompt() const;
        void OpenApiSettingsBuffers();
        void SaveApiSettings();
        void OpenVisionBuffers();
        void SaveVisionSettings();
        void ResetVisionSettings();
        AI_ASSISTANT::AiChatConfig SnapshotConfig() const;

        std::vector<AI_ASSISTANT::AiChatMessage> messages_{};
        std::mutex messageMutex_{};
        std::vector<std::string> reasoningLog_{};
        std::mutex reasoningMutex_{};
        std::mutex streamMutex_{};
        mutable std::mutex stateMutex_{};
        std::string streamingPartial_{};
        bool streamingActive_ = false;
        char inputBuffer_[4096] = {};
        char rawRequestBuffer_[2048] = {};
        bool showCapabilities_ = false;
        bool showRawRequest_ = false;
        bool showApiSettings_ = false;
        bool showApiKey_ = false;
        bool showVisionKey_ = false;
        bool hasRawResult_ = false;
        size_t lastMessageCount_ = 0u;
        char apiKeyBuffer_[256] = {};
        char baseUrlBuffer_[512] = {};
        char modelBuffer_[128] = {};
        char visionApiKeyBuffer_[256] = {};
        char visionBaseUrlBuffer_[512] = {};
        char visionModelBuffer_[128] = {};
        std::string bridgeStatus_{};
        AiSceneActionCallback sceneActionCallback_{};
        AiContextCallback contextCallback_{};
        AI_ASSISTANT::AiRequestResult rawResult_{};
        AI_ASSISTANT::AiChatConfig config_{};
        bool configLoaded_ = false;
        std::atomic<bool> busy_{ false };
        std::atomic<bool> stopRequested_{ false };
        std::thread worker_{};
    };

} // namespace HIKARI::EDITOR
