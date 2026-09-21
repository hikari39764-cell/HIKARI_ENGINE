#pragma once
#include "HIKARI_AI_ChatClient.h"

namespace HIKARI::EDITOR::AI_ASSISTANT {
// トランスポートの状態。編集はストリーム完了・検証の後にのみ実行する。
struct AiToolReply {
    nlohmann::json calls = nlohmann::json::array();
    std::string reasoning, finishReason, error;
    void ReadDelta(const nlohmann::json& chunk);
    bool Validate(std::string& outError) const;
    AiChatMessage Message(const std::string& content) const;
};
nlohmann::json BuildAiTools();
}
