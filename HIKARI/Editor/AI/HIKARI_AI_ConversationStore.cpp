#include "HIKARI_AI_ConversationStore.h"
#include <fstream>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {
constexpr size_t kMaximumConversationBytes = 64u * 1024u * 1024u;
bool ValidId(const std::string& id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '-';
    });
}
bool Read(const std::filesystem::path& path, nlohmann::json& json) {
    std::error_code error;
    if (std::filesystem::file_size(path, error) > kMaximumConversationBytes || error)
        return false;
    std::ifstream input(path, std::ios::binary);
    json = nlohmann::json::parse(input, nullptr, false);
    return json.is_object() && json.contains("version") && json["version"] == 1 && json.contains("title") &&
           json["title"].is_string() && json.contains("messages") && json["messages"].is_array();
}
nlohmann::json EncodeMessages(const std::vector<AiChatMessage>& messages) {
    auto result = nlohmann::json::array();
    for (const auto& message : messages) {
        nlohmann::json item{{"role", message.role},
                            {"content", message.content},
                            {"data", message.data},
                            {"images", nlohmann::json::array()}};
        for (const auto& image : message.images)
            item["images"].push_back({{"path", image.path}, {"label", image.label}, {"detail", image.detail}});
        result.push_back(std::move(item));
    }
    return result;
}
std::vector<AiChatMessage> DecodeMessages(const nlohmann::json& items) {
    std::vector<AiChatMessage> result;
    for (const auto& item : items) {
        AiChatMessage message{item.at("role"), item.at("content")};
        if (message.role != "user" && message.role != "assistant" && message.role != "tool")
            continue;
        if (item.contains("data") && item["data"].is_object())
            message.data = item["data"];
        if (item.contains("images"))
            for (const auto& image : item["images"])
                message.images.push_back({image.at("path"), image.value("label", std::string{}),
                                          image.value("detail", std::string("auto"))});
        result.push_back(std::move(message));
    }
    return result;
}
} // namespace
std::vector<AiConversationSummary> ListAiConversations(const std::filesystem::path& folder) {
    std::vector<std::pair<std::filesystem::file_time_type, AiConversationSummary>> ordered;
    std::error_code error;
    std::filesystem::directory_iterator iterator(folder, error), end;
    while (!error && iterator != end) {
        const auto path = iterator->path();
        if (path.extension() == ".json" && ValidId(path.stem().string())) {
            nlohmann::json data;
            if (Read(path, data))
                ordered.push_back({iterator->last_write_time(error),
                                   {path.stem().string(), data.value("title", std::string("Conversation"))}});
        }
        iterator.increment(error);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<AiConversationSummary> result;
    for (auto& entry : ordered)
        result.push_back(std::move(entry.second));
    return result;
}
bool LoadAiConversation(const std::filesystem::path& folder, const std::string& id,
                        std::vector<AiChatMessage>& messages, std::string& error, AiTaskCheckpoint* task) {
    try {
        nlohmann::json data;
        if (!ValidId(id) || !Read(folder / (id + ".json"), data) || !data.at("messages").is_array()) {
            error = "对话记录不存在或格式无效。";
            return false;
        }
        auto loaded = DecodeMessages(data.at("messages"));
        AiTaskCheckpoint checkpoint;
        if (data.contains("task") && data["task"].is_object()) {
            const auto& saved = data["task"];
            checkpoint.active = saved.value("active", false);
            checkpoint.prompt = saved.value("prompt", std::string{});
            checkpoint.context = saved.value("context", std::string{});
            if (saved.contains("messages"))
                checkpoint.messages = DecodeMessages(saved["messages"]);
            checkpoint.active = checkpoint.active && !checkpoint.messages.empty();
        }
        if (task)
            *task = std::move(checkpoint);
        messages = std::move(loaded);
        error.clear();
        return true;
    } catch (...) {
        error = "无法读取对话记录，原文件已保留。";
        return false;
    }
}
bool SaveAiConversation(const std::filesystem::path& folder, const std::string& id,
                        const std::vector<AiChatMessage>& messages, std::string& error, const AiTaskCheckpoint* task) {
    try {
        if (!ValidId(id) || folder.empty()) {
            error = "对话保存位置无效。";
            return false;
        }
        nlohmann::json data{{"version", 1}, {"title", "Conversation"}, {"messages", nlohmann::json::array()}};
        data["messages"] = EncodeMessages(messages);
        for (const auto& message : messages)
            if (message.role == "user" && !message.content.empty()) {
                data["title"] = message.content.substr(0, message.content.find('\n'));
                break;
            }
        if (task && task->active)
            data["task"] = {{"active", true},
                            {"prompt", task->prompt},
                            {"context", task->context},
                            {"messages", EncodeMessages(task->messages)}};
        const auto bytes = data.dump();
        if (bytes.size() > kMaximumConversationBytes) {
            error = "Conversation exceeds the storage limit; the previous checkpoint is retained.";
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        const auto destination = folder / (id + ".json"), temporary = folder / (id + ".tmp");
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << bytes;
            output.flush();
            if (!output) {
                error = "对话写入失败。";
                return false;
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error = "无法替换对话记录。";
            return false;
        }
#else
        std::filesystem::rename(temporary, destination, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
#endif
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "无法保存对话记录，原文件已保留。";
        return false;
    }
}
} // namespace HIKARI::EDITOR::AI_ASSISTANT
