#pragma once

#include <string>
#include <string_view>
#include <vector>

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
        void Draw(bool& open);

    private:
        char requestBuffer_[2048] = {};
        std::string lastRequest_{};
        AI_ASSISTANT::AiRequestResult lastResult_{};
        bool hasResult_ = false;
    };

} // namespace HIKARI::EDITOR
