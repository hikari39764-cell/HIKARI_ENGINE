#include "HIKARI_AI_RequestContext.h"

#include <algorithm>
#include <set>

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {
using Json = nlohmann::json;
constexpr size_t kImageHistoryBudget = 6;
constexpr size_t kNoIndex = static_cast<size_t>(-1);

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
    Json data = message.data;
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
    for (const auto* key : {"durationMs", "usage", "_usage", "images", "request"})
        data.erase(key);
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
            auto identities = Identities(value);
            if (!identities.empty())
                result[key] = std::move(identities);
        }
    }
    return result;
}

Json ReceiptData(const Json& data) {
    if (!data.is_object())
        return data;
    Json result = Identities(data);
    if (data.contains("results") && data["results"].is_array()) {
        result["results"] = Json::array();
        for (const auto& child : data["results"])
            result["results"].push_back(ReceiptData(child));
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
                            "property",
                            "appliedValue",
                            "appliedPatch",
                            "appliedChanges",
                            "adjusted",
                            "dirty",
                            "pending",
                            "documentChanged",
                            "stateNeedsRefresh",
                            "hints",
                            "batchStopped",
                            "message",
                            "diagnostics",
                            "operationKind",
                            "changedFields",
                            "schemaComplete",
                            "unavailableFields",
                            "archivedSteps",
                            "archivedFailures",
                            "supersededWrites"}) {
        if (data.contains(key))
            result[key] = data[key];
    }
    result["detailsOmitted"] = true;
    return result;
}

Json Receipt(const AiChatMessage& message) {
    Json result = ReceiptData(CompactData(message));
    if (!message.content.empty() && !result.contains("message"))
        result["message"] = message.content;
    return result;
}

struct ToolGroup {
    size_t begin = 0;
    size_t end = 0;
    std::vector<size_t> results;
    size_t imageCount = 0;
    size_t fullBytes = 0;
    size_t receiptBytes = 0;
    bool closed = false;
    bool reviewed = false;
};

std::vector<ToolGroup> FindToolGroups(const std::vector<AiChatMessage>& messages, size_t prefixMessages) {
    std::vector<ToolGroup> result;
    std::vector<bool> assigned(messages.size(), false);
    size_t lastAssistant = kNoIndex;
    for (size_t index = prefixMessages; index < messages.size(); ++index)
        if (messages[index].role == "assistant")
            lastAssistant = index;
    for (size_t begin = prefixMessages; begin < messages.size(); ++begin) {
        const auto& message = messages[begin];
        if (message.role != "assistant" || !message.data.contains("_toolCalls") ||
            !message.data["_toolCalls"].is_array())
            continue;
        std::set<std::string> pending;
        for (const auto& call : message.data["_toolCalls"])
            if (call.is_object() && call.contains("id") && call["id"].is_string())
                pending.insert(call["id"].get<std::string>());
        if (pending.empty())
            continue;

        ToolGroup group;
        group.begin = begin;
        group.end = begin;
        size_t lastResult = begin;
        for (size_t index = begin + 1; index < messages.size(); ++index) {
            const auto& candidate = messages[index];
            if (candidate.role == "assistant") {
                if (pending.empty()) {
                    group.closed = true;
                    group.reviewed = true;
                    group.end = lastResult;
                }
                break;
            }
            if (candidate.role != "tool")
                continue;
            const auto found = candidate.data.find("_toolCallId");
            if (found == candidate.data.end() || !found->is_string())
                continue;
            const auto id = found->get<std::string>();
            if (pending.erase(id) != 0) {
                group.results.push_back(index);
                lastResult = index;
                group.imageCount += candidate.images.size();
                assigned[index] = true;
            }
        }
        if (!group.reviewed && pending.empty()) {
            group.closed = true;
            group.end = lastResult;
        }
        if (!group.results.empty()) {
            result.push_back(std::move(group));
        }
    }
    for (size_t index = prefixMessages; index < messages.size(); ++index) {
        const auto& message = messages[index];
        if (message.role != "tool" || assigned[index])
            continue;
        ToolGroup group;
        group.begin = index;
        group.end = index;
        group.results.push_back(index);
        group.imageCount = message.images.size();
        if (!message.data.contains("_toolCallId")) {
            group.reviewed = lastAssistant != kNoIndex && lastAssistant > index;
            group.closed = group.reviewed;
        }
        result.push_back(std::move(group));
    }
    for (auto& group : result)
        for (const size_t index : group.results) {
            const auto& message = messages[index];
            size_t imageReferenceBytes = 0;
            for (const auto& image : message.images)
                imageReferenceBytes += 24 + image.path.size() + image.label.size();
            group.fullBytes += message.content.size() + CompactData(message).dump().size() + imageReferenceBytes;
            group.receiptBytes += Receipt(message).dump().size() + imageReferenceBytes;
        }
    std::sort(result.begin(), result.end(),
              [](const auto& left, const auto& right) { return left.begin < right.begin; });
    return result;
}

size_t RawIndex(size_t requestIndex, size_t prefixMessages) {
    return requestIndex >= prefixMessages ? requestIndex - prefixMessages : kNoIndex;
}

size_t NormalizeBoundary(size_t boundary, const std::vector<ToolGroup>& groups, size_t prefixMessages) {
    size_t valid = 0;
    for (const auto& group : groups) {
        if (!group.closed || !group.reviewed)
            break;
        const size_t end = group.end - prefixMessages + 1;
        if (end > boundary)
            break;
        valid = end;
    }
    return valid;
}

void CompactToolHistory(size_t prefixMessages, size_t budgetBytes, const std::vector<ToolGroup>& groups,
                        AiRequestContextState& state) {
    size_t bytes = 0;
    for (const auto& group : groups) {
        const bool compacted =
            group.closed && group.reviewed && group.end - prefixMessages + 1 <= state.compactedThroughMessage;
        bytes += compacted ? group.receiptBytes : group.fullBytes;
    }
    if (bytes <= budgetBytes)
        return;
    const size_t targetBytes = budgetBytes / 2;
    for (const auto& group : groups) {
        const size_t rawEnd = group.end - prefixMessages + 1;
        if (rawEnd <= state.compactedThroughMessage)
            continue;
        if (!group.closed || !group.reviewed)
            break;
        state.compactedThroughMessage = rawEnd;
        if (group.receiptBytes >= group.fullBytes)
            bytes += group.receiptBytes - group.fullBytes;
        else
            bytes -= (std::min)(bytes, group.fullBytes - group.receiptBytes);
        if (bytes <= targetBytes)
            break;
    }
}

size_t RetainedToolImageCount(const std::vector<AiChatMessage>& messages, size_t prefixMessages,
                              size_t retiredThrough) {
    size_t count = 0;
    for (size_t index = prefixMessages; index < messages.size(); ++index) {
        const size_t raw = RawIndex(index, prefixMessages);
        if (messages[index].role == "tool" && raw >= retiredThrough)
            count += messages[index].images.size();
    }
    return count;
}

void RetireReviewedImages(const std::vector<AiChatMessage>& messages, size_t prefixMessages,
                          const std::vector<ToolGroup>& groups, AiRequestContextState& state) {
    state.retiredImageThroughMessage = NormalizeBoundary(state.retiredImageThroughMessage, groups, prefixMessages);
    size_t retained = RetainedToolImageCount(messages, prefixMessages, state.retiredImageThroughMessage);
    if (retained <= kImageHistoryBudget)
        return;
    constexpr size_t lowWaterImages = kImageHistoryBudget / 2;
    size_t latestImageGroup = kNoIndex;
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i].imageCount != 0)
            latestImageGroup = i;
    for (size_t i = 0; i < groups.size() && retained > lowWaterImages; ++i) {
        const auto& group = groups[i];
        const size_t rawEnd = group.end - prefixMessages + 1;
        if (rawEnd <= state.retiredImageThroughMessage)
            continue;
        if (!group.closed || !group.reviewed || i == latestImageGroup)
            break;
        state.retiredImageThroughMessage = rawEnd;
        retained = RetainedToolImageCount(messages, prefixMessages, state.retiredImageThroughMessage);
    }
}

std::string ImageLabel(const AiImageAttachment& image, size_t index) {
    return "Image " + std::to_string(index + 1) + (image.label.empty() ? std::string{} : ": " + image.label);
}

std::vector<AiChatMessage> ProjectMessages(const std::vector<AiChatMessage>& messages, size_t prefixMessages,
                                           const std::vector<ToolGroup>& groups, const AiRequestContextState& state,
                                           AiReasoningContentPolicy reasoningPolicy) {
    std::vector<bool> compacted(messages.size(), false);
    for (const auto& group : groups) {
        if (!group.closed || !group.reviewed || group.end - prefixMessages + 1 > state.compactedThroughMessage)
            continue;
        for (const size_t index : group.results)
            compacted[index] = true;
    }
    size_t latestUserImages = kNoIndex;
    for (size_t index = prefixMessages; index < messages.size(); ++index) {
        if (messages[index].role == "user" && !messages[index].images.empty())
            latestUserImages = index;
    }
    std::vector<AiChatMessage> result;
    result.reserve(messages.size());
    for (size_t index = 0; index < messages.size(); ++index) {
        auto item = messages[index];
        const size_t raw = RawIndex(index, prefixMessages);
        if (item.role == "tool") {
            const std::string callId = item.data.value("_toolCallId", std::string{});
            if (compacted[index]) {
                Json receipt = Receipt(item);
                receipt.erase("_toolCallId");
                item.content = receipt.dump();
            } else {
                Json data = CompactData(item);
                data.erase("_toolCallId");
                if (!data.empty())
                    item.content += "\n" + data.dump();
            }
            item.data = Json::object();
            if (!callId.empty())
                item.data["_toolCallId"] = callId;
        } else if (reasoningPolicy == AiReasoningContentPolicy::Omit) {
            item.data.erase("_reasoningContent");
        }

        if (!item.images.empty()) {
            bool includeImages = false;
            if (item.role == "user") {
                includeImages = index == latestUserImages;
                if (!includeImages)
                    item.content += "\nEarlier reference image payload omitted; the latest reference set is attached.";
            } else if (item.role == "tool") {
                includeImages = raw != kNoIndex && raw >= state.retiredImageThroughMessage;
                for (size_t imageIndex = 0; imageIndex < item.images.size(); ++imageIndex) {
                    item.content +=
                        "\n" + ImageLabel(item.images[imageIndex], imageIndex) + ": " + item.images[imageIndex].path;
                    if (!includeImages)
                        item.content += " (payload omitted; use context.image to reopen)";
                }
            }
            if (!includeImages)
                item.images.clear();
        }
        result.push_back(std::move(item));
    }
    return result;
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
    if (evidence.contains("results")) {
        bool changed = false;
        for (const auto& child : evidence["results"])
            changed = Observe(child) || changed;
        return changed;
    }
    return evidence_.insert(evidence.dump()).second;
}

std::vector<AiChatMessage> BuildAiRequestContext(const std::vector<AiChatMessage>& messages, int toolHistoryKiB,
                                                 AiRequestContextState* state, size_t prefixMessages,
                                                 AiReasoningContentPolicy reasoningPolicy) {
    AiRequestContextState temporary;
    AiRequestContextState& projection = state ? *state : temporary;
    prefixMessages = (std::min)(prefixMessages, messages.size());
    const auto groups = FindToolGroups(messages, prefixMessages);
    projection.compactedThroughMessage = NormalizeBoundary(projection.compactedThroughMessage, groups, prefixMessages);
    projection.retiredImageThroughMessage =
        NormalizeBoundary(projection.retiredImageThroughMessage, groups, prefixMessages);
    const size_t budgetKiB = static_cast<size_t>(std::clamp(toolHistoryKiB, 8, 256));
    RetireReviewedImages(messages, prefixMessages, groups, projection);
    CompactToolHistory(prefixMessages, budgetKiB * 1024, groups, projection);
    return ProjectMessages(messages, prefixMessages, groups, projection, reasoningPolicy);
}
} // namespace HIKARI::EDITOR::AI_ASSISTANT
