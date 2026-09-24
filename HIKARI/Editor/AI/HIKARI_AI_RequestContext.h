#pragma once
#include "HIKARI_AI_ChatClient.h"
#include <set>

namespace HIKARI::EDITOR::AI_ASSISTANT {
// Full editor receipts stay in local history; this projection alone goes to the model.
std::vector<AiChatMessage> BuildAiRequestContext(const std::vector<AiChatMessage>& messages, int toolHistoryKiB);
// 成功した呼び出し回数ではなく、結果に新しい証拠があるかを見る。
class AiProgressTracker {
public:
    bool Observe(const nlohmann::json& result);

private:
    std::set<std::string> evidence_;
};
} // namespace HIKARI::EDITOR::AI_ASSISTANT
