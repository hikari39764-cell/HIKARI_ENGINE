#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "Editor/AI/HIKARI_AI_AssistantPanel.h"

namespace HIKARI::EDITOR::AI_ASSISTANT {

    // Local loopback HTTP bridge that exposes AI actions to external agents
    // (for example Codex or a game-jam tool). Binds to 127.0.0.1 only and
    // executes requests through the same executor as the AI Assistant panel.
    class AiLocalBridge {
    public:
        using Executor = std::function<AiRequestResult(std::string_view)>;
        using CapabilityProvider =
            std::function<std::vector<AiCapability>()>;

        AiLocalBridge();
        ~AiLocalBridge();

        AiLocalBridge(const AiLocalBridge&) = delete;
        AiLocalBridge& operator=(const AiLocalBridge&) = delete;

        bool Start(uint16_t port, Executor executor, std::string& outError);
        void Stop();
        void SetCapabilityProvider(CapabilityProvider callback);

        bool IsRunning() const;
        uint16_t GetPort() const;
        std::string GetEndpoint() const;

    private:
        void AcceptLoop();
        void HandleConnection(
            uintptr_t clientSocket,
            const Executor& executor);

        std::atomic<bool> running_{ false };
        std::atomic<bool> stopRequested_{ false };
        std::thread thread_{};
        mutable std::mutex stateMutex_{};
        uintptr_t listener_ = 0u;
        uint16_t port_ = 0u;
        std::string endpoint_;
        Executor executor_{};
        CapabilityProvider capabilityProvider_{};
    };

} // namespace HIKARI::EDITOR::AI_ASSISTANT
