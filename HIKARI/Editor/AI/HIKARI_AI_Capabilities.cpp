#include "HIKARI_AI_Types.h"
#include "HIKARI_AI_ChatClient.h"
#include "HIKARI_AI_MessageText.h"
#include <algorithm>
#include <map>
#include <sstream>
#include <utility>
namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {
std::string FoldSearch(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return text;
}
std::string CatalogVersion(const std::vector<AiCapability>& capabilities) {
    uint64_t hash = 14695981039346656037ull;
    for (const auto& capability : capabilities) {
        for (unsigned char c : DescribeAiCapability(capability, true).dump()) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
    }
    std::ostringstream text;
    text << std::hex << hash;
    return text.str();
}
int MatchScore(const AiCapability& capability, const std::string& keyword) {
    if (keyword.empty())
        return 1;
    const auto id = FoldSearch(capability.id);
    std::string haystack = id + " " + FoldSearch(capability.summary) + " " + FoldSearch(capability.workspace);
    for (const auto& term : capability.searchTerms)
        haystack += " " + FoldSearch(term);
    if (id == keyword)
        return 1000;
    int score = haystack.find(keyword) != std::string::npos ? 100 : 0;
    std::istringstream words(keyword);
    std::string word;
    while (words >> word) {
        if (id.find(word) != std::string::npos)
            score += 20;
        else if (haystack.find(word) != std::string::npos)
            score += 5;
    }
    return score;
}
} // namespace
nlohmann::json DescribeAiCapability(const AiCapability& capability, bool parameters, bool forModel) {
    auto item = capability.semantics;
    if (forModel) {
        item.erase("searchTerms");
        item.erase("parameters");
        item.erase("status");
        const auto permission = item.find("permission");
        if (permission != item.end() && permission->is_string() && permission->get<std::string>() == "unspecified")
            item.erase("permission");
        for (const std::string_view flag : {"requiresDocumentSession", "requiresCleanDocument", "entryPoint"}) {
            const auto value = item.find(std::string(flag));
            if (value != item.end() && value->is_boolean() && !value->get<bool>())
                item.erase(value);
        }
    }
    item["id"] = capability.id;
    if (!forModel || capability.status != "stable")
        item["status"] = capability.status;
    item["summary"] = capability.summary;
    item["workspace"] = capability.workspace;
    if (parameters) {
        item["parameters"] = nlohmann::json::array();
        for (const auto& p : capability.parameters) {
            nlohmann::json field{{"name", p.name}, {"type", p.type}};
            if (!forModel || p.required)
                field["required"] = p.required;
            if (!forModel || !p.description.empty())
                field["description"] = p.description;
            if (!p.schema.empty())
                field["schema"] = p.schema;
            item["parameters"].push_back(std::move(field));
        }
    } else
        item.erase("parameters");
    return item;
}

const std::vector<AiCapability>& BuiltInCapabilities() {
    static const std::vector<AiCapability> capabilities = [] {
        std::vector<AiCapability> entries{
            {"query.capabilities",
             "Search available operations by purpose. Returns ranked matches with parameters; no filters returns a compact workspace index.",
             "stable",
             {{"workspace", "string", false, "Workspace id"},
              {"keyword", "string", false, "Purpose, operation name or search terms"},
              {"includeParameters", "boolean", false, "Include parameter schemas (default true)"},
              {"offset", "integer", false, "Next page offset from previous result"},
              {"limit", "integer", false, "Results per page, 1 to 32 (default 12)"}}},
            {"help",
             "Describe a registered action. Pass topic=action id.",
             "stable",
             {{"topic", "string", true, "Action id"}}},
            {"context.image",
             "Reopen an existing local image for visual review, without a new capture or separate model call.",
             "stable",
             {{"path", "string", true, "Path from a previous image result"}}},
            {"context.compare_images",
             "Review candidate images side by side with a reference image (user attachment or project file) and report concrete differences before adjusting.",
             "stable",
             {{"reference", "string", true, "Reference image path"},
              {"candidates", "json", true, "Array of 1 to 5 candidate image paths, such as capture results"},
              {"focus", "string", false, "What to compare, for example silhouette, color or timing"}}}};
        for (auto& entry : entries)
            entry.semantics = {{"kind", "query"}, {"entryPoint", true}, {"requiresDocumentSession", false}};
        return entries;
    }();
    return capabilities;
}

AiRequestResult SubmitAiRequest(std::string_view requestJson, const std::vector<AiCapability>& capabilities) {
    const std::string action = Trim(ExtractJsonField(requestJson, "action"));
    if (action == "context.image") {
        const auto path = ExtractJsonField(requestJson, "path");
        if (path.empty())
            return {false, "MISSING_PATH", "Provide the path of the image to review."};
        AiRequestResult result{true, "OK", "Original image reopened for review."};
        result.data["images"] = nlohmann::json::array({{{"path", path}, {"label", "Review"}}});
        return result;
    }
    if (action == "query.capabilities") {
        AiRequestResult result{};
        result.ok = true;
        result.message = "Available capabilities from the current engine registry.";
        const auto workspace = ExtractJsonField(requestJson, "workspace");
        const auto keyword = FoldSearch(ExtractJsonField(requestJson, "keyword"));
        const auto arguments = nlohmann::json::parse(requestJson);
        const bool includeParameters = arguments.value("includeParameters", true);
        const int offset = (std::max)(0, arguments.value("offset", 0));
        const int limit = std::clamp(arguments.value("limit", 12), 1, 32);
        result.data["operationKind"] = "query";
        result.data["knowledgeScope"] = "capability";
        result.data["catalogVersion"] = CatalogVersion(capabilities);
        result.data["capabilities"] = nlohmann::json::array();
        std::map<std::string, int> workspaces;
        std::vector<std::pair<int, const AiCapability*>> matches;
        for (const AiCapability& capability : capabilities) {
            if (capability.status != "stable")
                continue;
            ++workspaces[capability.workspace];
            if (!workspace.empty() && capability.workspace != workspace)
                continue;
            const int score = MatchScore(capability, keyword);
            if (score)
                matches.emplace_back(score, &capability);
        }
        result.data["workspaces"] = workspaces;
        if (workspace.empty() && keyword.empty())
            return result;
        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second->id < b.second->id;
        });
        result.data["total"] = matches.size();
        for (size_t i = static_cast<size_t>(offset); i < matches.size() && i < static_cast<size_t>(offset) + limit; ++i)
            result.data["capabilities"].push_back(DescribeAiCapability(*matches[i].second, includeParameters, true));
        if (static_cast<size_t>(offset) + limit < matches.size())
            result.data["nextOffset"] = offset + limit;
        return result;
    }
    if (action == "context.compare_images") {
        constexpr size_t kMaximumCandidates = 5u;
        const auto arguments = nlohmann::json::parse(requestJson, nullptr, false);
        const std::string reference = arguments.value("reference", std::string{});
        const auto candidates = arguments.value("candidates", nlohmann::json::array());
        if (reference.empty() || !candidates.is_array() || candidates.empty() ||
            candidates.size() > kMaximumCandidates) {
            return {false, "INVALID_IMAGES", "Provide reference and 1 to 5 candidate image paths."};
        }
        AiRequestResult result{true, "OK",
                               "Compare each candidate with the reference. List concrete differences in "
                               "silhouette, composition, color and value, lighting and motion before editing; "
                               "do not claim a match that is not visible."};
        const std::string focus = arguments.value("focus", std::string{});
        if (!focus.empty()) {
            result.message += " Focus: " + focus;
        }
        result.data["images"] = nlohmann::json::array({{{"path", reference}, {"label", "Reference"}}});
        for (size_t index = 0; index < candidates.size(); ++index) {
            if (!candidates[index].is_string()) {
                return {false, "INVALID_IMAGES", "Candidate image paths must be strings."};
            }
            result.data["images"].push_back(
                {{"path", candidates[index].get<std::string>()}, {"label", "Candidate " + std::to_string(index + 1)}});
        }
        return result;
    }
    if (action == "help") {
        const std::string topic = ExtractJsonField(requestJson, "topic");
        for (const auto& capability : capabilities)
            if (capability.id == topic) {
                AiRequestResult result{true, "OK", capability.summary};
                result.data = DescribeAiCapability(capability, true, true);
                result.data["operationKind"] = "query";
                result.data["knowledgeScope"] = "capability";
                result.data["catalogVersion"] = CatalogVersion(capabilities);
                return result;
            }
        return {false, "UNKNOWN_ACTION", "未找到这个动作，请使用 query.capabilities 获取实际能力。"};
    }

    AiRequestResult result{};
    result.ok = false;
    result.message = action.empty() ? "请求缺少 action 字段。示例：{\"action\":\"query.capabilities\"}"
                                    : "不支持的 action：" + action + "。先调用 query.capabilities 查询边界。";
    result.details = {
        "supported: query.capabilities",
    };
    return result;
}

} // namespace HIKARI::EDITOR::AI_ASSISTANT
