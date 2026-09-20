#include "HIKARI_AI_ConversationStore.h"
#include <fstream>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {
bool ValidId(const std::string& id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(),id.end(),[](char c) {
        return (c>='0'&&c<='9') || (c>='a'&&c<='z') || c=='-';
    });
}
bool Read(const std::filesystem::path& path, nlohmann::json& json) {
    std::error_code error;
    if (std::filesystem::file_size(path,error)>8*1024*1024 || error) return false;
    std::ifstream input(path,std::ios::binary);
    json = nlohmann::json::parse(input,nullptr,false);
    return json.is_object() && json.contains("version") && json["version"] == 1 &&
        json.contains("title") && json["title"].is_string() &&
        json.contains("messages") && json["messages"].is_array();
}
}
std::vector<AiConversationSummary> ListAiConversations(const std::filesystem::path& folder) {
    std::vector<std::pair<std::filesystem::file_time_type,AiConversationSummary>> ordered;
    std::error_code error;
    std::filesystem::directory_iterator iterator(folder,error), end;
    while (!error && iterator!=end) {
        const auto path=iterator->path();
        if (path.extension()==".json" && ValidId(path.stem().string())) {
            nlohmann::json data;
            if (Read(path,data)) ordered.push_back({iterator->last_write_time(error),
                {path.stem().string(),data.value("title",std::string("Conversation"))}});
        }
        iterator.increment(error);
    }
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) { return a.first>b.first; });
    std::vector<AiConversationSummary> result;
    for (auto& entry:ordered) result.push_back(std::move(entry.second));
    return result;
}
bool LoadAiConversation(const std::filesystem::path& folder,const std::string& id,
    std::vector<AiChatMessage>& messages,std::string& error) {
    try {
        nlohmann::json data;
        if (!ValidId(id) || !Read(folder/(id+".json"),data) || !data.at("messages").is_array()) {
            error="对话记录不存在或格式无效。"; return false;
        }
        std::vector<AiChatMessage> loaded;
        for (const auto& item:data.at("messages")) {
            AiChatMessage message{item.at("role"),item.at("content")};
            if (message.role!="user" && message.role!="assistant" && message.role!="tool") continue;
            if (item.contains("data") && item["data"].is_object()) message.data=item["data"];
            if (item.contains("images")) for (const auto& image:item["images"]) {
                if (message.images.size()>=6) break;
                message.images.push_back({image.at("path"),image.value("label",std::string{}),image.value("detail",std::string("auto"))});
            }
            loaded.push_back(std::move(message));
            if (loaded.size()>=120) break;
        }
        messages=std::move(loaded); error.clear(); return true;
    } catch (...) { error="无法读取对话记录，原文件已保留。"; return false; }
}
bool SaveAiConversation(const std::filesystem::path& folder,const std::string& id,
    const std::vector<AiChatMessage>& messages,std::string& error) {
    try {
    if (!ValidId(id) || folder.empty()) { error="对话保存位置无效。"; return false; }
    nlohmann::json data{{"version",1},{"title","Conversation"},{"messages",nlohmann::json::array()}};
    for (const auto& message:messages) {
        if (message.role=="user" && data["title"]=="Conversation" && !message.content.empty())
            data["title"]=message.content.substr(0,message.content.find('\n'));
        nlohmann::json item{{"role",message.role},{"content",message.content},{"data",message.data},{"images",nlohmann::json::array()}};
        for (const auto& image:message.images) item["images"].push_back({{"path",image.path},{"label",image.label},{"detail",image.detail}});
        data["messages"].push_back(std::move(item));
    }
    std::error_code ec;
    std::filesystem::create_directories(folder,ec);
    if (ec) { error=ec.message(); return false; }
    const auto destination=folder/(id+".json"), temporary=folder/(id+".tmp");
    { std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
      output<<data.dump(2); output.flush(); if (!output) { error="对话写入失败。"; return false; } }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        error="无法替换对话记录。"; return false;
    }
#else
    std::filesystem::rename(temporary,destination,ec);
    if (ec) { error=ec.message(); return false; }
#endif
    error.clear(); return true;
    } catch (const std::exception&) { error="无法保存对话记录，原文件已保留。"; return false; }
}
}
