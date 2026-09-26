#pragma once
#include "HIKARI_AI_ChatClient.h"
#include <cstddef>
#include <set>

namespace HIKARI::EDITOR::AI_ASSISTANT {
// Boundaries index the raw task.messages sequence. The raw transcript remains authoritative for recovery and replay
// protection; only these projection decisions are checkpointed.
struct AiRequestContextState {
    size_t compactedThroughMessage = 0;
    size_t retiredImageThroughMessage = 0;
};

// Full editor receipts stay in local history; this projection alone goes to the model.
std::vector<AiChatMessage> BuildAiRequestContext(
    const std::vector<AiChatMessage>& messages, int toolHistoryKiB, AiRequestContextState* state = nullptr,
    size_t prefixMessages = 0, AiReasoningContentPolicy reasoningPolicy = AiReasoningContentPolicy::Preserve);
// 成功した呼び出し回数ではなく、結果に新しい証拠があるかを見る。
class AiProgressTracker {
public:
    bool Observe(const nlohmann::json& result);

private:
    std::set<std::string> evidence_;
};
} // namespace HIKARI::EDITOR::AI_ASSISTANT
