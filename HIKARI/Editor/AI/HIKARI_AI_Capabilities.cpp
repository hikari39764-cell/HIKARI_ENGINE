#include "HIKARI_AI_Types.h"
#include "HIKARI_AI_ChatClient.h"
#include "HIKARI_AI_MessageText.h"
#include "HIKARI_AI_VisionClient.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include <algorithm>
#include <utility>
namespace HIKARI::EDITOR::AI_ASSISTANT {
const std::vector<AiCapability>& BuiltInCapabilities() {
    static const std::vector<AiCapability> capabilities{
        {"query.capabilities","Discover operations; optional workspace and keyword filters. Use help for parameters.","stable",
            {{"workspace","string",false,"Workspace id"},{"keyword","string",false,"Action id or description contains text"}}},
        {"query.material_templates","List built-in material templates.","stable"},
        {"help","Describe a registered action. Pass topic=action id.","stable",{{"topic","string",true,"Action id"}}},
        {"context.image","Reopen an existing local image for visual review, without a new capture or separate model call.","stable",{{"path","string",true,"Path from a previous image result"}}},
        {"vision.analyze_image","Optional separate vision model. Main-model image inputs arrive directly from captures and attachments.","stable",{{"path","string",true,"Local image path"},{"prompt","string",false,"Analysis instructions"}}}
    };
    return capabilities;
}

AiRequestResult SubmitAiRequest(
    std::string_view requestJson,
    const std::vector<AiCapability>& capabilities,
    const std::function<bool()>& shouldCancel) {
    const std::string action = Trim(ExtractAction(requestJson));
    if (action == "context.image") {
        const auto path = ExtractJsonField(requestJson, "path");
        if (path.empty()) return {false, "MISSING_PATH", "Provide the path of the image to review."};
        AiRequestResult result{true, "OK", "Original image reopened for review."};
        result.data["images"] = nlohmann::json::array({{{"path", path}, {"label", "Review"}}});
        return result;
    }
    if (action == "list_capabilities" || action == "query.capabilities") {
        AiRequestResult result{};
        result.ok = true;
        result.message = "可用动作。用 help 查询参数。";
        const auto workspace=ExtractJsonField(requestJson,"workspace");
        const auto keyword=ExtractJsonField(requestJson,"keyword");
        result.data["capabilities"]=nlohmann::json::array();
        for (const AiCapability& capability : capabilities) {
            if (!workspace.empty() && capability.workspace!=workspace) continue;
            if (!keyword.empty() && capability.id.find(keyword)==std::string::npos && capability.summary.find(keyword)==std::string::npos) continue;
            result.data["capabilities"].push_back({{"id",capability.id},{"summary",capability.summary},{"workspace",capability.workspace}});
        }
        return result;
    }
    if (action == "list_material_templates" || action == "query.material_templates") {
        AiRequestResult result{};
        result.ok = true;
        result.message = "内置材质模板：";
        for (const ASSETS::MATERIAL::MaterialTemplateInfo& info :
             ASSETS::MATERIAL::GetBuiltInMaterialTemplates()) {
            result.details.push_back(std::string(info.displayName));
        }
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
            result.data={{"action",capability.id},{"parameters",nlohmann::json::array()}};
            for (const auto& p:capability.parameters)
                result.data["parameters"].push_back({{"name",p.name},{"type",p.type},{"required",p.required},{"description",p.description}});
            return result;
        }
        return {false,"UNKNOWN_ACTION","未找到这个动作，请使用 query.capabilities 获取实际能力。"};
    }

    AiRequestResult result{};
    result.ok = false;
    result.message = action.empty()
        ? "请求缺少 action 字段。示例：{\"action\":\"list_capabilities\"}"
        : "不支持的 action：" + action + "。先调用 list_capabilities 查询边界。";
    result.details = {
        "supported: list_capabilities",
        "supported: list_material_templates",
    };
    return result;
}

}
