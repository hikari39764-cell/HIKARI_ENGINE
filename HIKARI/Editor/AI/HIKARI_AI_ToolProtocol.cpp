#include "HIKARI_AI_ToolProtocol.h"
#include <set>

namespace HIKARI::EDITOR::AI_ASSISTANT {
using Json = nlohmann::json;
Json BuildAiTools() {
    return Json::array({
        {{"type", "function"}, {"function", {
            {"name", "editor_actions"},
            {"description", "Execute registered HIKARI editor actions. Discover operations with query.capabilities; help returns their parameter schema. Actions run in order; failure skips the remaining batch. Never guess IDs. Related actions with known inputs may be batched."},
            {"parameters", {{"type", "object"}, {"properties", {
                {"actions", {{"type", "array"}, {"minItems", 1}, {"items", {
                    {"type", "object"}, {"properties", {{"action", {{"type", "string"}}}}},
                    {"required", Json::array({"action"})}, {"additionalProperties", true}}}}}}},
                {"required", Json::array({"actions"})}}}}}},
        {{"type", "function"}, {"function", {
            {"name", "task_finish"},
            {"description", "Deliver the final user-facing answer. Discussion can finish directly. For authorized work, perform it first and report actual results and verification limits; a promise to start is progress, not completion. Use needs_input for a concrete missing user answer, blocked for an actual obstacle."},
            {"parameters", {{"type", "object"}, {"properties", {
                {"status", {{"type", "string"}, {"enum", Json::array({"completed", "needs_input", "blocked"})}}},
                {"summary", {{"type", "string"}}}}},
                {"required", Json::array({"status", "summary"})}, {"additionalProperties", false}}}}}}
    });
}

void AiToolReply::ReadDelta(const Json& chunk) {
    if (!chunk.is_object()) { error = "Invalid response chunk."; return; }
    if (chunk.contains("error")) { error = "Provider returned an error in the response stream."; return; }
    const auto choices = chunk.find("choices");
    if (choices == chunk.end() || !choices->is_array() || choices->empty()) return;
    const auto& choice = (*choices)[0];
    if (choice.contains("finish_reason") && choice["finish_reason"].is_string())
        finishReason = choice["finish_reason"].get<std::string>();
    const auto delta = choice.find("delta");
    if (delta == choice.end() || !delta->is_object()) return;
    if (delta->contains("reasoning_content") && (*delta)["reasoning_content"].is_string())
        reasoning += (*delta)["reasoning_content"].get<std::string>();
    const auto incoming = delta->find("tool_calls");
    if (incoming == delta->end()) return;
    if (!incoming->is_array()) { error = "Invalid tool call list."; return; }
    for (const auto& item : *incoming) {
        if (!item.is_object() || !item.contains("index") || !item["index"].is_number_integer()) {
            error = "Tool call index is missing."; return;
        }
        const int index = item["index"].get<int>();
        if (index < 0 || index >= 64) { error = "Invalid tool call index."; return; }
        while (calls.size() <= static_cast<size_t>(index))
            calls.push_back({{"id", ""}, {"type", "function"}, {"function", {{"name", ""}, {"arguments", ""}}}});
        auto& call = calls[index];
        if (item.contains("id") && item["id"].is_string()) {
            const auto id = item["id"].get<std::string>();
            if (call["id"] != "" && call["id"] != id) { error = "Conflicting tool call ID."; return; }
            call["id"] = id;
        }
        if (item.contains("function") && item["function"].is_object()) {
            for (const char* key : {"name", "arguments"}) {
                const auto part = item["function"].find(key);
                if (part != item["function"].end() && part->is_string())
                    call["function"][key] = call["function"][key].get<std::string>() + part->get<std::string>();
            }
        }
    }
}

bool AiToolReply::Validate(std::string& outError) const {
    outError = error;
    if (!outError.empty()) return false;
    if (finishReason != "stop" && finishReason != "tool_calls") {
        outError = "响应被截断或缺少完成标记，未执行其中的动作。"; return false;
    }
    std::set<std::string> ids;
    for (const auto& call : calls) {
        const auto id = call.value("id", std::string{});
        const auto& function = call["function"];
        const auto arguments = Json::parse(function["arguments"].get<std::string>(), nullptr, false);
        if (id.empty() || !ids.insert(id).second || function["name"] == "" || !arguments.is_object()) {
            outError = "工具调用不完整或 ID 重复，未执行本轮动作。"; return false;
        }
    }
    if (finishReason == "tool_calls" && calls.empty()) {
        outError = "模型未返回可执行的工具调用。"; return false;
    }
    return true;
}

AiChatMessage AiToolReply::Message(const std::string& content) const {
    AiChatMessage message{"assistant", content};
    if (!calls.empty()) message.data["_toolCalls"] = calls;
    if (!reasoning.empty()) message.data["_reasoningContent"] = reasoning;
    return message;
}
}
