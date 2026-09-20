#include "HIKARI_AI_RequestContext.h"
#include <algorithm>
#include <set>
#include <map>

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {
using Json = nlohmann::json;

Json UnflattenedStructure(const Json& node) {
    if (!node.is_object()) return Json::object();
    Json result = Json::object();
    for (const auto& [key, value] : node.items()) {
        if (key == "id") result[key] = value;
        else if (value.is_object()) {
            auto child = UnflattenedStructure(value);
            if (!child.empty()) result[key] = std::move(child);
        } else if (value.is_array() && (value.empty() ||
            !std::all_of(value.begin(), value.end(), [](const auto& item) { return item.is_number(); }))) {
            result[key] = value;
        }
    }
    return result;
}

bool IsMutation(const std::string& action) {
    const auto last = action.substr(action.find_last_of('.') + 1);
    return last == "set" || last == "add" || last == "remove" || last == "duplicate" ||
        last == "edit" || last == "move" || last == "connect" || last == "disconnect" ||
        last == "rename" || last == "reparent";
}

Json CompactData(const AiChatMessage& message) {
    auto data = message.data;
    if (!data.is_object()) return data;
    const auto action = data.value("action", std::string{});
    if (action == "vfx.target.describe" && data.value("ok", true) &&
        data.contains("serializedValue") && data.contains("properties") && data["properties"].is_array()) {
        // properties が持つ値を二重送信しない。列挙されない構造・曲線は保持。
        data["structure"] = UnflattenedStructure(data["serializedValue"]);
        data.erase("serializedValue");
    }
    for (const auto* key : {"durationMs", "usage", "_usage", "images"}) data.erase(key);
    if (action.ends_with(".property.set") && data.value("ok", true)) {
        const auto request = data.value("request", Json::object());
        const auto property = request.value("property", std::string{});
        if (data.contains("properties") && data["properties"].is_array()) {
            for (const auto& entry : data["properties"]) {
                if (entry.value("id", std::string{}) == property && entry.contains("value"))
                    data["appliedValue"] = entry["value"];
            }
        }
        data.erase("serializedValue");
        if (property.empty()) data["stateNeedsRefresh"] = true; // Legacy receipts have no write address.
    }
    if (IsMutation(action) && data.value("ok", true)) {
        data.erase("propertySchemas");
        data.erase("properties");
    }
    // A property write used to echo the entire serialized layer. Keep its address
    // and actual value; descriptions remain available through target.describe.
    if (action.ends_with(".property.set") && data.contains("request")) {
        const auto& request = data["request"];
        if (request.contains("property")) data["property"] = request["property"];
    }
    return data;
}

Json Identities(const Json& node) {
    if (node.is_array()) {
        Json result = Json::array();
        for (const auto& item : node) {
            auto ids = Identities(item);
            if (!ids.empty()) result.push_back(std::move(ids));
        }
        return result;
    }
    Json result = Json::object();
    if (!node.is_object()) return result;
    for (const auto& [key, value] : node.items()) {
        if (key == "id" || key.ends_with("Id") || key.ends_with("Ids") || key.ends_with("Guid") ||
            key == "name" || key == "modules") result[key] = value;
        else if (key != "request" && key != "propertySchemas" && key != "properties" &&
                 (value.is_object() || value.is_array())) {
            auto ids = Identities(value);
            if (!ids.empty()) result[key] = std::move(ids);
        }
    }
    return result;
}

Json Receipt(const Json& data) {
    if (!data.is_object()) return data;
    Json result = Identities(data);
    for (const auto* key : {"action", "ok", "code", "sessionId", "workspace", "targetId",
            "emitterId", "layerId", "nodeId", "id", "name", "kind", "revision", "documentRevision",
            "vfxAssetGuid", "sourcePath", "request", "property", "appliedValue", "hints", "batchStopped",
            "diagnostics", "stateNeedsRefresh", "archivedSteps", "archivedFailures", "supersededWrites"}) {
        if (data.contains(key)) result[key] = data[key];
    }
    result["detailsOmitted"] = true;
    return result;
}

std::string QueryIdentity(const Json& data) {
    const auto action = data.value("action", std::string{});
    if (action != "help" && !action.starts_with("query.") && !action.ends_with(".query") &&
        !action.ends_with(".describe") && !action.ends_with(".list")) return {};
    return action + ":" + data.value("sessionId", std::string{}) + ":" +
        data.value("targetId", std::string{}) + ":" + data.value("request", Json::object()).dump();
}
}

std::vector<AiChatMessage> BuildAiRequestContext(
    const std::vector<AiChatMessage>& messages, int toolHistoryKiB) {
    std::vector<AiChatMessage> result;
    result.reserve(messages.size());
    size_t latestReply = messages.size(), reference = messages.size();
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto phase = messages[i].data.value("phase", std::string{});
        if (messages[i].role == "assistant" && phase != "failed" && phase != "stopped") latestReply = i;
        if (messages[i].role == "user" && !messages[i].images.empty()) reference = i;
    }
    // Preserve the user's current reference set. Generated previews are sent only
    // until the next model response; their paths/time/revision remain queryable.
    for (size_t i = 0; i < messages.size(); ++i) {
        auto item = messages[i];
        const bool preview = item.role == "tool" || item.data.contains("action");
        const bool includeImages = preview ? (latestReply == messages.size() || i > latestReply) : i == reference;
        if (!item.images.empty()) {
            Json images = Json::array();
            for (const auto& image : item.images) images.push_back({{"path", image.path}, {"label", image.label}});
            item.content += "\nimages " + images.dump();
            if (!includeImages) {
                item.images.clear();
                item.content += "\nPreviously sent image; context.image reopens it without recapturing.";
            }
        }
        if (item.role == "tool") item.data = CompactData(item);
        result.push_back(std::move(item));
    }
    // 同じターン内で確認済みの上書きだけを畳む。未読の結果・失敗・
    // Undo/Redo・画像・別の時間キーをまたいで値を消さない。
    std::map<std::string, size_t> latestWrites;
    for (size_t i = result.size(); i-- > 0;) {
        auto& item = result[i];
        if (item.role == "user") { latestWrites.clear(); continue; }
        if (item.role != "tool") continue;
        const auto action = item.data.value("action", std::string{});
        if (!item.data.value("ok", false) || action.ends_with(".undo") || action.ends_with(".redo") ||
            (IsMutation(action) && !action.ends_with(".property.set"))) {
            latestWrites.clear(); continue;
        }
        if (i > latestReply || latestReply == messages.size() || !item.images.empty() ||
            !action.ends_with(".property.set") || !item.data.contains("request")) continue;
        auto address = item.data["request"];
        if (!address.is_object() || !address.contains("property") || !address.contains("value")) continue;
        address.erase("value");
        const auto key = action + ":" + item.data.value("sessionId", std::string{}) + ":" + address.dump();
        const auto [found, inserted] = latestWrites.emplace(key, i);
        if (!inserted) {
            auto& retained = result[found->second].data;
            retained["supersededWrites"] = retained.value("supersededWrites", 0) + 1;
            item.content.clear();
            item.data = Json::object();
            item.role.clear();
        }
    }
    // A soft budget for tool detail, not a tokenizer estimate or an instruction
    // truncation limit. Never cut JSON or the newest response, IDs, or user text.
    size_t remaining = static_cast<size_t>(std::clamp(toolHistoryKiB, 8, 256)) * 1024;
    std::set<std::string> queries;
    for (auto it = result.rbegin(); it != result.rend(); ++it) {
        if (it->role != "tool") continue;
        const auto identity = QueryIdentity(it->data);
        const bool superseded = !identity.empty() && it->data.value("ok", true) && !queries.insert(identity).second;
        const size_t index = static_cast<size_t>(std::distance(result.begin(), it.base()) - 1);
        const bool unread = latestReply == messages.size() || index > latestReply;
        const size_t bytes = it->content.size() + it->data.dump().size();
        if (!unread && (superseded || bytes > remaining)) {
            auto receipt = Receipt(it->data);
            if (!superseded && it->data.contains("authoringSemantics"))
                receipt["authoringSemantics"] = it->data["authoringSemantics"];
            it->data = std::move(receipt);
        }
        remaining -= (std::min)(remaining, it->content.size() + it->data.dump().size());
        it->content += "\n" + it->data.dump();
        it->data = Json::object();
    }
    std::erase_if(result, [](const auto& item) { return item.content.empty() && item.images.empty(); });
    return result;
}
}
