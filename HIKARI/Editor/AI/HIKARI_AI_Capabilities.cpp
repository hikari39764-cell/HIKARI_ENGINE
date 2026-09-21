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
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}
std::string CatalogVersion(const std::vector<AiCapability>& capabilities) {
    uint64_t hash = 14695981039346656037ull;
    for (const auto& capability : capabilities) {
        for (unsigned char c : DescribeAiCapability(capability,true).dump()) {
            hash ^= c; hash *= 1099511628211ull;
        }
    }
    std::ostringstream text;
    text << std::hex << hash;
    return text.str();
}
int MatchScore(const AiCapability& capability, const std::string& keyword) {
    if (keyword.empty()) return 1;
    const auto id = FoldSearch(capability.id);
    std::string haystack = id + " " + FoldSearch(capability.summary) + " " + FoldSearch(capability.workspace);
    for (const auto& term : capability.searchTerms) haystack += " " + FoldSearch(term);
    if (id == keyword) return 1000;
    int score = haystack.find(keyword) != std::string::npos ? 100 : 0;
    std::istringstream words(keyword);
    std::string word;
    while (words >> word) {
        if (id.find(word) != std::string::npos) score += 20;
        else if (haystack.find(word) != std::string::npos) score += 5;
    }
    return score;
}
}
nlohmann::json DescribeAiCapability(const AiCapability& capability, bool parameters) {
    auto item = capability.semantics;
    item["id"] = capability.id;
    item["status"] = capability.status;
    item["summary"] = capability.summary;
    item["workspace"] = capability.workspace;
    if (parameters) {
        item["parameters"] = nlohmann::json::array();
        for (const auto& p : capability.parameters) {
            auto field = nlohmann::json{{"name",p.name},{"type",p.type},
                {"required",p.required},{"description",p.description}};
            if (!p.schema.empty()) field["schema"] = p.schema;
            item["parameters"].push_back(std::move(field));
        }
    }
    return item;
}
const std::vector<AiCapability>& BuiltInCapabilities() {
    static const std::vector<AiCapability> capabilities = [] {
    std::vector<AiCapability> entries{
        {"query.capabilities","Search available operations by purpose. Returns ranked matches with parameters; no filters returns a compact workspace index.","stable",
            {{"workspace","string",false,"Workspace id"},{"keyword","string",false,"Purpose, operation name or search terms"},
             {"includeParameters","bool",false,"Include parameter schemas (default true)"},
             {"offset","integer",false,"Next page offset from previous result"},{"limit","integer",false,"Results per page, 1 to 32 (default 12)"}}},
        {"help","Describe a registered action. Pass topic=action id.","stable",{{"topic","string",true,"Action id"}}},
        {"context.image","Reopen an existing local image for visual review, without a new capture or separate model call.","stable",{{"path","string",true,"Path from a previous image result"}}},
        {"vision.analyze_image","Optional separate vision model. Main-model image inputs arrive directly from captures and attachments.","stable",{{"path","string",true,"Local image path"},{"prompt","string",false,"Analysis instructions"}}}
    };
    for (auto& entry : entries) entry.semantics = {{"kind","query"},{"entryPoint",true},{"requiresDocumentSession",false}};
    return entries;
    }();
    return capabilities;
}

AiRequestResult SubmitAiRequest(
    std::string_view requestJson,
    const std::vector<AiCapability>& capabilities,
    const std::function<bool()>& shouldCancel) {
    const std::string action = Trim(ExtractJsonField(requestJson,"action"));
    if (action == "context.image") {
        const auto path = ExtractJsonField(requestJson, "path");
        if (path.empty()) return {false, "MISSING_PATH", "Provide the path of the image to review."};
        AiRequestResult result{true, "OK", "Original image reopened for review."};
        result.data["images"] = nlohmann::json::array({{{"path", path}, {"label", "Review"}}});
        return result;
    }
    if (action == "query.capabilities") {
        AiRequestResult result{};
        result.ok = true;
        result.message = "Available capabilities from the current engine registry.";
        const auto workspace=ExtractJsonField(requestJson,"workspace");
        const auto keyword=FoldSearch(ExtractJsonField(requestJson,"keyword"));
        const auto arguments = nlohmann::json::parse(requestJson);
        const bool includeParameters = arguments.value("includeParameters",true);
        const int offset = (std::max)(0, arguments.value("offset",0));
        const int limit = std::clamp(arguments.value("limit",12),1,32);
        result.data["operationKind"] = "query";
        result.data["knowledgeScope"] = "capability";
        result.data["catalogVersion"] = CatalogVersion(capabilities);
        result.data["capabilities"]=nlohmann::json::array();
        std::map<std::string,int> workspaces;
        std::vector<std::pair<int,const AiCapability*>> matches;
        for (const AiCapability& capability : capabilities) {
            if (capability.status != "stable") continue;
            ++workspaces[capability.workspace];
            if (!workspace.empty() && capability.workspace!=workspace) continue;
            const int score = MatchScore(capability,keyword);
            if (score) matches.emplace_back(score,&capability);
        }
        result.data["workspaces"] = workspaces;
        if (workspace.empty() && keyword.empty()) return result;
        std::sort(matches.begin(),matches.end(),[](const auto& a,const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second->id < b.second->id;
        });
        result.data["total"] = matches.size();
        for (size_t i = static_cast<size_t>(offset); i < matches.size() && i < static_cast<size_t>(offset) + limit; ++i)
            result.data["capabilities"].push_back(DescribeAiCapability(*matches[i].second,includeParameters));
        if (static_cast<size_t>(offset) + limit < matches.size()) result.data["nextOffset"] = offset + limit;
        return result;
    }
    if (action == "vision.analyze_image") {
        AiRequestResult result{};
        std::string image = ExtractJsonField(requestJson, "path");
        if (image.empty()) image = ExtractJsonField(requestJson, "image");
        if (image.empty()) {
            result.ok = false;
            result.message = "缺少 image 字段（本地图片路径）";
            result.details = {
                "示例：{\"action\":\"vision.analyze_image\","
                "\"image\":\"C:/x.png\",\"prompt\":\"描述这张图\"}",
            };
            return result;
        }
        AiChatConfig vision{};
        if (!LoadAiVisionConfig(vision)) {
            result.ok = false;
            result.message =
                "未配置视觉 API Key（可在 API Settings 的 Vision 区配置）";
            return result;
        }
        const std::string prompt =
            ExtractJsonField(requestJson, "prompt");
        vision.shouldCancel=shouldCancel;
        std::string text;
        std::string error;
        if (!RunVisionAnalysis(
                vision, image, prompt, text, error)) {
            result.ok = false;
            result.message = "识图失败：" + error;
            return result;
        }
        result.ok = true;
        result.message = text;
        return result;
    }
    if (action == "help") {
        const std::string topic=ExtractJsonField(requestJson,"topic");
        for (const auto& capability:capabilities) if (capability.id==topic) {
            AiRequestResult result{true,"OK",capability.summary};
            result.data = DescribeAiCapability(capability,true);
            result.data["operationKind"] = "query";
            result.data["knowledgeScope"] = "capability";
            result.data["catalogVersion"] = CatalogVersion(capabilities);
            return result;
        }
        return {false,"UNKNOWN_ACTION","未找到这个动作，请使用 query.capabilities 获取实际能力。"};
    }

    AiRequestResult result{};
    result.ok = false;
    result.message = action.empty()
        ? "请求缺少 action 字段。示例：{\"action\":\"query.capabilities\"}"
        : "不支持的 action：" + action + "。先调用 query.capabilities 查询边界。";
    result.details = {
        "supported: query.capabilities",
    };
    return result;
}

}
