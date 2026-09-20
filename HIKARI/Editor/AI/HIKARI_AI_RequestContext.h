#pragma once
#include "HIKARI_AI_ChatClient.h"

namespace HIKARI::EDITOR::AI_ASSISTANT {
// Full editor receipts stay in local history; this projection alone goes to the model.
std::vector<AiChatMessage> BuildAiRequestContext(
    const std::vector<AiChatMessage>& messages, int toolHistoryKiB);
}
