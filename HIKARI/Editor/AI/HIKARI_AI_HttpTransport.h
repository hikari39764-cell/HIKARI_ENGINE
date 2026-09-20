#pragma once
#include "HIKARI_AI_ChatClient.h"
namespace HIKARI::EDITOR::AI_ASSISTANT {
bool RunAiHttpStream(const AiChatConfig& config,const std::string& body,
    const std::function<bool(const std::string&)>& onChunk,std::string& error);
}
