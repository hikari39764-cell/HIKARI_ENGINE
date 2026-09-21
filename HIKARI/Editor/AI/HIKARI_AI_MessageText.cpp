#include "HIKARI_AI_MessageText.h"
#include "HIKARI_AI_Types.h"
#include "HIKARI_AI_ChatClient.h"
#include "Core/Text/HIKARI_Trim.h"
#include <algorithm>
#include <cctype>
#include <utility>
namespace HIKARI::EDITOR::AI_ASSISTANT {
std::string TruncateUtf8(std::string_view text,size_t maxBytes) {
    if (text.size()<=maxBytes) return std::string(text);
    size_t end=maxBytes;
    while (end && (static_cast<unsigned char>(text[end]) & 0xc0)==0x80) --end;
    return std::string(text.substr(0,end))+"…";
}
std::string Trim(std::string value) {
    return TEXT::TrimCopy(std::move(value));
}

std::string StripActionJsonForDisplay(std::string text) {
    // ストリーム途中のタグも隠す。操作内容は実行ログで確認できる。
    for (const auto marker : {"<｜", "<|", "<tool_call", "</tool_call"}) {
        const auto position = text.find(marker);
        if (position != std::string::npos) text.resize(position);
    }
    std::string result;
    size_t cursor = 0u;
    while (cursor < text.size()) {
        const size_t newline = text.find('\n', cursor);
        const std::string line = newline == std::string::npos
            ? text.substr(cursor)
            : text.substr(cursor, newline - cursor);
        cursor = newline == std::string::npos
            ? text.size()
            : newline + 1u;
        const std::string trimmed = Trim(line);
        if (trimmed == "<") continue;
        if (!trimmed.empty() && (trimmed.front() == '{' || trimmed.front() == '[')) {
            // 複数行・未完の JSON をチャット本文に流さない。
            const size_t open = cursor - line.size() - (newline == std::string::npos ? 0u : 1u) + line.find_first_not_of(" \t\r");
            int depth = 0; bool quoted = false, escaped = false;
            size_t end = open;
            for (; end < text.size(); ++end) {
                const char ch = text[end];
                if (quoted) { if (escaped) escaped=false; else if (ch=='\\') escaped=true; else if(ch=='"') quoted=false; continue; }
                if(ch=='"') quoted=true;
                else if(ch=='{' || ch=='[') ++depth;
                else if((ch=='}' || ch==']') && --depth==0) { ++end; break; }
            }
            const auto block = text.substr(open,end-open);
            if (depth != 0 || block.find("\"action") != std::string::npos) {
                cursor = end;
                continue;
            }
        }
        if (trimmed.rfind("```", 0u) == 0u &&
            (trimmed.find("json") != std::string::npos ||
             trimmed.find("JSON") != std::string::npos)) {
            while (cursor < text.size()) {
                const size_t close = text.find("```", cursor);
                if (close == std::string::npos) {
                    cursor = text.size();
                    break;
                }
                cursor = close + 3u;
                const size_t closeNewline = text.find('\n', cursor);
                cursor = closeNewline == std::string::npos
                    ? text.size()
                    : closeNewline + 1u;
                break;
            }
            continue;
        }
        if (!result.empty()) {
            result += '\n';
        }
        result += line;
    }
    return Trim(result);
}

}
