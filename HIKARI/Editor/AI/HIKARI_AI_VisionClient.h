#pragma once

#include <string>

#include "Editor/AI/HIKARI_AI_ChatClient.h"

namespace HIKARI::EDITOR::AI_ASSISTANT {

    bool LoadAiVisionConfig(AiChatConfig& outConfig);
    bool SaveAiVisionConfig(const AiChatConfig& config);
    bool ResetAiVisionConfig();
    bool RunVisionAnalysis(
        const AiChatConfig& config,
        const std::string& imagePath,
        const std::string& prompt,
        std::string& outText,
        std::string& outError);

} // namespace HIKARI::EDITOR::AI_ASSISTANT
