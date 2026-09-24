#include "HIKARI_AI_RequestContext.h"
#include <algorithm>
#include <set>
#include <map>

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {
using Json = nlohmann::json;

Json UnflattenedStructure(const Json& node) {
    if (!node.is_object())
        return Json::object();
    Json result = Json::object();
    for (const auto& [key, value] : node.items()) {
        if (key == "id")
            result[key] = value;
        else if (value.is_object()) {
            auto child = UnflattenedStructure(value);
            if (!child.empty())
                result[key] = std::move(child);
        } else if (value.is_array() && (value.empty() || !std::all_of(value.begin(), value.end(), [](const auto& item) {
                                            return item.is_number();
                                        }))) {
            result[key] = value;
        }
    }
    return result;
}

Json CompactData(const AiChatMessage& message) {
    auto data = message.data;
    if (!data.is_object())
        return data;
    if (data.contains("results") && data["results"].is_array()) {
        for (auto& result : data["results"]) {
            AiChatMessage child{"tool", "", {}, result};
            result = CompactData(child);
        }
    }
    const auto kind = data.value("operationKind", std::string{});
    if (kind == "query" && data.value("ok", true) && data.contains("serializedValue") && data.contains("properties") &&
        data["properties"].is_array()) {
        // properties が持つ値を二重送信しない。列挙されない構造・曲線は保持。
        data["structure"] = UnflattenedStructure(data["serializedValue"]);
        data.erase("serializedValue");
    }
    for (const auto* key : {"durationMs", "usage", "_usage", "images"})
        data.erase(key);
    if ((kind == "mutation" && data.value("request", Json::object()).contains("value")) && data.value("ok", true)) {
        const auto request = data.value("request", Json::object());
        const auto property = request.value("property", std::string{});
        if (data.contains("properties") && data["properties"].is_array()) {
            for (const auto& entry : data["properties"]) {
                if (entry.value("id", std::string{}) == property && entry.contains("value"))
                    data["appliedValue"] = entry["value"];
            }
        }
        data.erase("serializedValue");
        if (property.empty())
            data["stateNeedsRefresh"] = true; // Legacy receipts have no write address.
    }
    if (kind == "mutation" && data.value("ok", true)) {
        data.erase("propertySchemas");
        data.erase("properties");
    }
    // A property write used to echo the entire serialized layer. Keep its address
    // and actual value; descriptions remain available through target.describe.
    if ((kind == "mutation" && data.value("request", Json::object()).contains("value")) && data.contains("request")) {
        const auto& request = data["request"];
        if (request.contains("property"))
            data["property"] = request["property"];
    }
    return data;
}

Json Identities(const Json& node) {
    if (node.is_array()) {
        Json result = Json::array();
        for (const auto& item : node) {
            auto ids = Identities(item);
            if (!ids.empty())
                result.push_back(std::move(ids));
        }
        return result;
    }
    Json result = Json::object();
    if (!node.is_object())
        return result;
    for (const auto& [key, value] : node.items()) {
        if (key == "id" || key.ends_with("Id") || key.ends_with("Ids") || key.ends_with("Guid") || key == "name" ||
            key == "modules")
            result[key] = value;
        else if (key != "request" && key != "propertySchemas" && key != "properties" &&
                 (value.is_object() || value.is_array())) {
            auto ids = Identities(value);
            if (!ids.empty())
                result[key] = std::move(ids);
        }
    }
    return result;
}

Json Receipt(const Json& data) {
    if (!data.is_object())
        return data;
    Json result = Identities(data);
    // バッチ全体を圧縮しても各操作の成否・対象・失敗理由を失わない。
    if (data.contains("results") && data["results"].is_array()) {
        result["results"] = Json::array();
        for (const auto& child : data["results"])
            result["results"].push_back(Receipt(child));
    }
    for (const auto* key : {"action",
                            "ok",
                            "code",
                            "sessionId",
                            "workspace",
                            "targetId",
                            "emitterId",
                            "layerId",
                            "nodeId",
                            "id",
                            "name",
                            "kind",
                            "revision",
                            "documentRevision",
                            "vfxAssetGuid",
                            "sourcePath",
                            "request",
                            "property",
                            "appliedValue",
                            "hints",
                            "batchStopped",
                            "_toolCallId",
                            "message",
                            "diagnostics",
                            "operationKind",
                            "operationSchema",
                            "changedFields",
                            "documentChanged",
                            "schemaComplete",
                            "unavailableFields",
                            "stateNeedsRefresh",
                            "archivedSteps",
                            "archivedFailures",
                            "supersededWrites"}) {
        if (data.contains(key))
            result[key] = data[key];
    }
    result["detailsOmitted"] = true;
    return result;
}

// 宣言された読み取りだけをまとめる。操作名の接頭辞や分野には依存しない。
void CompactReviewedResults(Json& data, bool unread, size_t& remaining, std::set<std::string>& queries,
                            std::set<std::string>& changedSessions, std::set<std::string>& knownCapabilities,
                            std::string& catalogVersion) {
    if (!data.is_object())
        return;
    if (data.contains("results") && data["results"].is_array()) {
        for (auto it = data["results"].rbegin(); it != data["results"].rend(); ++it)
            CompactReviewedResults(*it, unread, remaining, queries, changedSessions, knownCapabilities, catalogVersion);
        return;
    }
    const bool knowledge = data.value("knowledgeScope", std::string{}) == "capability";
    const auto version = data.value("catalogVersion", std::string{});
    if (knowledge && catalogVersion.empty())
        catalogVersion = version;
    const auto kind = data.value("operationKind", std::string{});
    const auto session = data.value("sessionId", std::string{});
    const bool query = kind == "query" && data.value("ok", false);
    const auto identity = data.value("request", Json::object()).dump();
    const bool duplicate = query && data.contains("request") && !queries.insert(identity).second;
    const bool stale = knowledge ? (!version.empty() && version != catalogVersion)
                                 : query && (changedSessions.contains(session) || changedSessions.contains("*"));
    if (knowledge && !stale) {
        const auto remember = [&](const Json& item) {
            if (!item.contains("parameters"))
                return true;
            return knownCapabilities.insert(version + ":" + item.value("id", std::string{})).second;
        };
        if (data.contains("capabilities")) {
            auto retained = Json::array();
            auto known = Json::array();
            for (const auto& capability : data["capabilities"]) {
                const bool first = remember(capability);
                if (unread || first)
                    retained.push_back(capability);
                else
                    known.push_back(capability.value("id", std::string{}));
            }
            data["capabilities"] = std::move(retained);
            if (!known.empty())
                data["knownCapabilityIds"] = std::move(known);
        } else if (data.contains("parameters") && !remember(data) && !unread) {
            data.erase("parameters");
            data["schemaInNewerResult"] = true;
        }
    }
    const auto bytes = data.dump().size();
    if (!unread && duplicate) {
        data = {{"action", data.value("action", std::string{})}, {"ok", true}, {"superseded", true}};
    } else if (!unread && (stale || (bytes > remaining && !knowledge))) {
        data = Receipt(data);
        if (stale)
            data["stateNeedsRefresh"] = true;
    }
    // 能力説明は直近の検索結果を保持。現行の引数定義まで流水帳と一緒に落とさない。
    remaining -= (std::min)(remaining, data.dump().size());
    if (kind == "mutation" && data.value("ok", false))
        changedSessions.insert(session.empty() ? "*" : session);
}
// 確認済みの古い呼び出しは結果の台帳へ畳む。最新の native call/result は
// 丸ごと残し、ID の対応を壊したり未読の結果を削ったりしない。
void FoldReviewedProtocol(std::vector<AiChatMessage>& messages, size_t budget) {
    std::vector<size_t> starts;
    size_t bytes = 0;
    for (size_t i = 0; i < messages.size(); ++i) {
        bytes += messages[i].content.size() + messages[i].data.dump().size();
        if (messages[i].data.contains("_toolCalls"))
            starts.push_back(i);
    }
    for (size_t group = 0; group + 2 < starts.size() && bytes > budget; ++group) {
        const size_t begin = starts[group], end = starts[group + 1];
        std::set<std::string> pending;
        for (const auto& call : messages[begin].data["_toolCalls"])
            pending.insert(call.at("id"));
        bool hasImage = false;
        for (size_t i = begin + 1; i < end; ++i) {
            hasImage = hasImage || !messages[i].images.empty();
            if (messages[i].data.contains("_toolCallId"))
                pending.erase(messages[i].data["_toolCallId"].get<std::string>());
        }
        if (!pending.empty() || hasImage)
            continue;
        const size_t before = messages[begin].data.dump().size();
        messages[begin].data.erase("_toolCalls");
        messages[begin].data.erase("_reasoningContent");
        bytes -= (std::min)(bytes, before - messages[begin].data.dump().size());
        for (size_t i = begin + 1; i < end; ++i)
            messages[i].data.erase("_toolCallId");
    }
}
} // namespace

bool AiProgressTracker::Observe(const nlohmann::json& result) {
    if (!result.is_object() || !result.value("ok", false))
        return false;
    if (result.value("documentChanged", true) == false)
        return false;
    auto evidence = result;
    for (const auto* key : {"durationMs", "usage", "_usage", "message", "request", "images", "poll", "details"})
        evidence.erase(key);
    // パスやメッセージだけ変わる再取得は、新しい制作結果として扱わない。
    if (evidence.contains("results")) {
        bool changed = false;
        for (const auto& child : evidence["results"])
            changed = Observe(child) || changed;
        return changed;
    }
    return evidence_.insert(evidence.dump()).second;
}

std::vector<AiChatMessage> BuildAiRequestContext(const std::vector<AiChatMessage>& messages, int toolHistoryKiB) {
    std::vector<AiChatMessage> result;
    result.reserve(messages.size());
    size_t latestReply = messages.size(), reference = messages.size();
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto phase = messages[i].data.value("phase", std::string{});
        if (messages[i].role == "assistant" && phase != "failed" && phase != "stopped")
            latestReply = i;
        if (messages[i].role == "user" && !messages[i].images.empty())
            reference = i;
    }
    // Preserve the user's current reference set. Generated previews are sent only
    // until the next model response; their paths/time/revision remain queryable.
    for (size_t i = 0; i < messages.size(); ++i) {
        auto item = messages[i];
        const bool preview = item.role == "tool" || item.data.contains("action");
        const bool includeImages = preview ? (latestReply == messages.size() || i > latestReply) : i == reference;
        if (!item.images.empty()) {
            Json images = Json::array();
            for (const auto& image : item.images)
                images.push_back({{"path", image.path}, {"label", image.label}});
            item.content += "\nimages " + images.dump();
            if (!includeImages) {
                item.images.clear();
                item.content += "\nPreviously sent image; context.image reopens it without recapturing.";
            }
        }
        if (item.role == "tool")
            item.data = CompactData(item);
        result.push_back(std::move(item));
    }
    FoldReviewedProtocol(result, static_cast<size_t>(std::clamp(toolHistoryKiB, 8, 256)) * 1024);
    // A soft budget for tool detail, not a tokenizer estimate or an instruction
    // truncation limit. Never cut JSON or the newest response, IDs, or user text.
    size_t remaining = static_cast<size_t>(std::clamp(toolHistoryKiB, 8, 256)) * 1024;
    std::set<std::string> queries, changedSessions, knownCapabilities;
    std::string catalogVersion;
    for (auto it = result.rbegin(); it != result.rend(); ++it) {
        if (it->role == "user") {
            queries.clear();
            changedSessions.clear();
            continue;
        }
        if (it->role != "tool")
            continue;
        const size_t index = static_cast<size_t>(std::distance(result.begin(), it.base()) - 1);
        const bool unread = latestReply == messages.size() || index > latestReply;
        CompactReviewedResults(it->data, unread, remaining, queries, changedSessions, knownCapabilities,
                               catalogVersion);
        const auto callId = it->data.value("_toolCallId", std::string{});
        it->data.erase("_toolCallId");
        it->content += "\n" + it->data.dump();
        it->data = Json::object();
        if (!callId.empty())
            it->data["_toolCallId"] = callId;
    }
    std::erase_if(result, [](const auto& item) {
        return item.content.empty() && item.images.empty() && !item.data.contains("_toolCalls");
    });
    return result;
}
} // namespace HIKARI::EDITOR::AI_ASSISTANT
