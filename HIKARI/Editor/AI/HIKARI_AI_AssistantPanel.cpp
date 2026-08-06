#include "HIKARI_AI_AssistantPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/AI/HIKARI_AI_VisionClient.h"

#include "Core/Text/HIKARI_Trim.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
constexpr int kDefaultToolRounds = 12;
constexpr int kMaxToolRoundsCeiling = 30;
} // namespace

namespace AI_ASSISTANT {

namespace {

std::string Trim(std::string value) {
    return TEXT::TrimCopy(std::move(value));
}

std::string ExtractAction(std::string_view json) {
    const auto action = json.find("\"action\"");
    if (action == std::string_view::npos) {
        return {};
    }
    const auto colon = json.find(':', action);
    if (colon == std::string_view::npos) {
        return {};
    }
    const auto quote = json.find('"', colon + 1u);
    if (quote == std::string_view::npos) {
        return {};
    }
    const auto end = json.find('"', quote + 1u);
    if (end == std::string_view::npos) {
        return {};
    }
    return std::string(json.substr(quote + 1u, end - quote - 1u));
}

std::string ExtractPlanLine(std::string_view text) {
    const auto marker = text.find("计划:");
    if (marker == std::string_view::npos) {
        return {};
    }
    const auto lineEnd = text.find('\n', marker);
    const std::string line = lineEnd == std::string_view::npos
        ? std::string(text.substr(marker))
        : std::string(text.substr(marker, lineEnd - marker));
    return Trim(line);
}

bool IsPlanOnlyReply(std::string_view text) {
    return text.find("计划:") != std::string_view::npos ||
        text.find("计划：") != std::string_view::npos ||
        text.find("Plan:") != std::string_view::npos ||
        text.find("プラン:") != std::string_view::npos;
}

std::string StripActionJsonForDisplay(std::string text) {
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
        if (trimmed.size() >= 2u &&
            trimmed.front() == '{' &&
            trimmed.back() == '}' &&
            (trimmed.find("\"action\"") != std::string::npos ||
             trimmed.find("\"actions\"") != std::string::npos)) {
            continue;
        }
        if (!result.empty()) {
            result += '\n';
        }
        result += line;
    }
    return result;
}

} // namespace

const std::vector<AiCapability>& QueryCapabilities() {
    static const std::vector<AiCapability> capabilities{
        { "chat.converse",
          "Conversational replies via the configured DashScope-compatible chat API; action JSON executes through SubmitAiRequest.",
          "stable" },
        { "vision.analyze_image",
          "Analyze a local image via the configured vision API; defaults to the vision-skill DashScope model.",
          "stable" },
        { "procedural.create_primitive",
          "Create a procedural primitive (box/sphere/plane/cylinder/capsule) with position, scale and size.",
          "stable" },
        { "scene.list_objects",
          "List scene objects with position and scale.",
          "stable" },
        { "scene.set_transform",
          "Set an existing object's position/rotation/scale by name.",
          "stable" },
        { "scene.create_object",
          "Create a scene object from a registered model asset.",
          "stable" },
        { "asset.list_models",
          "List registered model assets (id and source path).",
          "stable" },
        { "material.edit_parameter",
          "Edit a numeric parameter of the currently open material document.",
          "stable" },
        { "material.graph_edit",
          "Add/remove/connect/disconnect material graph nodes through the document command path.",
          "stable" },
        { "material.graph_query",
          "List material graph nodes with ids, pins and existing connections.",
          "stable" },
        { "material.node_info",
          "Return one node's semantic contract: summary, pin types, class and output route.",
          "stable" },
        { "material.node_list",
          "List discoverable material graph node types, optionally filtered by keyword or class.",
          "stable" },
        { "material.graph_layout",
          "Auto-layout material graph nodes by topology.",
          "stable" },
        { "material.verify",
          "Capture the material preview and analyze it with the vision API.",
          "stable" },
        { "budget.request",
          "Request a higher tool-round budget for the current conversation.",
          "stable" },
        { "capture.preview",
          "Capture the material preview to a PNG file for vision verification.",
          "stable" },
        { "help",
          "Query detailed usage and examples for an action.",
          "stable" },
        { "action.undo_last",
          "Undo the most recent AI-driven material or scene change.",
          "stable" },
        { "scene.snapshot",
          "Return a compact scene object snapshot for before/after comparison.",
          "stable" },
        { "scene.query",
          "Query scene objects by name filter with position and scale.",
          "stable" },
        { "asset.import",
          "Copy a supported source file into the project and queue its import.",
          "stable" },
        { "asset.import_status",
          "Report the current queued asset import progress.",
          "stable" },
        { "query.capabilities",
          "List the current AI-accessible capabilities and boundaries.",
          "stable" },
        { "query.material_templates",
          "List built-in material templates (PBR/Toon/Skin/Face/Hair/Eye/Cloth/Hybrid).",
          "stable" },
        { "material.compile",
          "Material graphs compile to bounded expression programs with diagnostics and budget.",
          "stable" },
        { "material.parameters",
          "Authored response parameters reach the runtime material program block.",
          "stable" },
        { "material.preview",
          "ToolPreview supports Studio Inspect, Scene Match and side-by-side compare.",
          "stable" },
        { "material.double_sided",
          "Authored double-sided materials publish the shader flag and back-face handling.",
          "stable" },
        { "asset.generate_model",
          "Generating new meshes or rigs from text is not supported.",
          "unsupported" },
        { "asset.generate_texture",
          "Generating textures from text is not supported inside the engine.",
          "unsupported" },
        { "animation.generate",
          "Generating animation clips from text is not supported.",
          "unsupported" },
    };
    return capabilities;
}

AiRequestResult SubmitAiRequest(std::string_view requestJson) {
    const std::string action = Trim(ExtractAction(requestJson));
    if (action == "list_capabilities") {
        AiRequestResult result{};
        result.ok = true;
        result.message = "当前 AI 可访问能力清单如下（status: stable/planned/unsupported）。";
        for (const AiCapability& capability : QueryCapabilities()) {
            result.details.push_back(
                capability.status + ": " + capability.id + " - " +
                capability.summary);
        }
        return result;
    }
    if (action == "list_material_templates") {
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
        const std::string image =
            ExtractJsonField(requestJson, "image");
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
        AiRequestResult result{};
        result.ok = true;
        const std::string topic =
            ExtractJsonField(requestJson, "topic");
        if (topic.empty()) {
            result.message =
                "输入 topic 查询具体用法，例如 "
                "{\"action\":\"help\",\"topic\":"
                "\"procedural.create_primitive\"}";
            return result;
        }
        result.message = "用法说明：" + topic;
        if (topic == "procedural.create_primitive") {
            result.details = {
                "{\"action\":\"procedural.create_primitive\","
                "\"kind\":\"box\",\"name\":\"cube\","
                "\"positionX\":0,\"positionY\":0,\"positionZ\":0,"
                "\"scaleX\":1,\"scaleY\":1,\"scaleZ\":1}",
                "kind: box/sphere/plane/grid_plane/cylinder/capsule",
            };
        } else if (topic == "scene.set_transform") {
            result.details = {
                "{\"action\":\"scene.set_transform\","
                "\"name\":\"cube\","
                "\"positionX\":0,\"positionY\":1,\"positionZ\":0,"
                "\"rotationY\":45,\"scaleX\":1}",
                "只提供需要修改的字段即可",
            };
        } else if (topic == "scene.create_object") {
            result.details = {
                "{\"action\":\"scene.create_object\","
                "\"model\":\"资产id或路径片段\",\"name\":\"角色\","
                "\"positionX\":0,\"positionZ\":0}",
                "先用 asset.list_models 查询可用模型",
            };
        } else if (topic == "material.edit_parameter") {
            result.details = {
                "{\"action\":\"material.edit_parameter\","
                "\"parameter\":\"shadowSoftness\",\"value\":0.08}",
                "parameter 支持全名或后缀；只改当前打开的材质",
            };
        } else if (topic == "material.graph_edit") {
            result.details = {
                "连线前先调用 material.graph_query 获取节点 id 和插槽："
                "{\"action\":\"material.graph_query\"}",
                "{\"action\":\"material.graph_edit\","
                "\"operation\":\"add_node\","
                "\"nodeType\":\"node.additive\",\"name\":\"Rim\"}",
                "创建颜色常量：{\"action\":\"material.graph_edit\","
                "\"operation\":\"add_node\",\"nodeType\":\"Constant\","
                "\"valueType\":\"Float4\",\"valueX\":1,\"valueY\":1,"
                "\"valueZ\":1,\"valueW\":1}",
                "修改节点：{\"action\":\"material.graph_edit\","
                "\"operation\":\"set_node\",\"node\":\"Node_5\","
                "\"property\":\"value\",\"valueType\":\"Float4\","
                "\"valueX\":0.5,\"valueY\":0.5,\"valueZ\":0.5,"
                "\"valueW\":1}",
                "nodeType 支持多种写法：Constant/Parameter/Add、"
                "node.constant/node.parameter，或成品名 node.additive/"
                "node.skinLighting",
                "{\"action\":\"material.graph_edit\","
                "\"operation\":\"connect\","
                "\"sourceNode\":\"Node_1\",\"sourcePin\":\"A\","
                "\"targetNode\":\"Node_2\",\"targetPin\":\"B\"}",
                "operation: add_node/remove_node/set_node/connect/disconnect；"
                "节点可用 name 或 id 引用；pin 名以 add_node 返回的 "
                "inputs/outputs 为准（例如 A/B/DiffuseResponse/Surface），"
                "sourcePin/targetPin 可省略或填 out/in 取第一个",
            };
        } else if (topic == "material.graph_query") {
            result.details = {
                "{\"action\":\"material.graph_query\"}",
                "可选 name 过滤："
                "{\"action\":\"material.graph_query\",\"name\":\"Rim\"}",
            };
        } else if (topic == "material.node_info") {
            result.details = {
                "{\"action\":\"material.node_info\","
                "\"nodeType\":\"noise.value\"}",
                "返回节点的语义、输入输出引脚类型、分类和输出路由；"
                "不确定节点含义时先查这个",
            };
        } else if (topic == "material.node_list") {
            result.details = {
                "{\"action\":\"material.node_list\"}",
                "{\"action\":\"material.node_list\","
                "\"keyword\":\"noise\"}",
                "{\"action\":\"material.node_list\","
                "\"class\":\"Math\"}",
                "返回可创建节点目录；创建节点前先查这个，避免猜节点名",
            };
        } else if (topic == "material.graph_layout") {
            result.details = {
                "{\"action\":\"material.graph_layout\"}",
                "按拓扑分层整理当前材质图节点位置",
            };
        } else if (topic == "material.verify") {
            result.details = {
                "{\"action\":\"material.verify\"}",
                "先截图再调用视觉 API 分析，返回描述与是否符合预期",
            };
        } else if (topic == "budget.request") {
            result.details = {
                "{\"action\":\"budget.request\","
                "\"rounds\":24,\"reason\":\"逐节点检查需要更多轮\"}",
                "默认 12 轮，上限 30；授予后每轮会返回剩余预算",
            };
        } else if (topic == "capture.preview") {
            result.details = {
                "{\"action\":\"capture.preview\","
                "\"path\":\"C:/temp/preview.png\"}",
                "不传 path 时保存到 %TEMP%/hikari-ai-preview.png",
            };
        } else if (topic == "vision.analyze_image") {
            result.details = {
                "{\"action\":\"vision.analyze_image\","
                "\"image\":\"C:/temp/preview.png\","
                "\"prompt\":\"描述这张图\"}",
            };
        } else if (topic == "scene.list_objects" ||
                   topic == "scene.snapshot" ||
                   topic == "scene.query") {
            result.details = {
                "{\"action\":\"scene.list_objects\"}",
                "scene.snapshot 返回同样的对象摘要，"
                "用于修改前后对比",
                "scene.query 支持 name 过滤："
                "{\"action\":\"scene.query\",\"name\":\"cube\"}",
            };
        } else if (topic == "asset.import" ||
                   topic == "asset.import_status") {
            result.details = {
                "{\"action\":\"asset.import\","
                "\"path\":\"C:/model.glb\"}",
                "asset.import_status 返回排队导入的进度",
            };
        } else {
            result.message =
                "没有 topic 的专用说明，请先用 list_capabilities "
                "查看能力清单。";
        }
        return result;
    }
    if (action == "create_material") {
        AiRequestResult result{};
        result.ok = false;
        result.message =
            "做不到：AI 还不能创建或保存材质资产（Assets 写命令尚未开放）。"
            "当前可先查询模板，再在编辑器里创建，之后由 AI 调参。";
        result.details = {
            "supported: list_capabilities",
            "supported: list_material_templates",
            "planned: material.edit_parameter",
            "planned: material.create_from_template",
        };
        return result;
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

} // namespace AI_ASSISTANT

AiAssistantPanel::AiAssistantPanel() {
    configLoaded_ = AI_ASSISTANT::LoadAiChatConfig(config_);
}

AiAssistantPanel::~AiAssistantPanel() {
    stopRequested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string AiAssistantPanel::BuildSystemPrompt() const {
    std::string prompt =
        "你是 HIKARI 引擎的 AI 编辑助手。你可以通过输出 JSON 调用引擎能力。\n"
        "批量规则：需要多个动作时，输出一个数组 {\"actions\":["
        "{\"action\":\"...\",\"...\":\"...\"},{\"action\":\"...\"}]}，"
        "引擎会按顺序全部执行并汇总；单个失败不会中断整批，"
        "结果里会标明失败项和原因。\n"
        "行规则：每个 JSON 必须是单独一行且整行都是 JSON，"
        "不要把它混在普通文字里，也不要在正文里举例 JSON；"
        "解释性回复（如说明失败原因）禁止输出整行动作 JSON，"
        "如需引用失败的 action 只写动作名（如 connect）。\n"
        "收尾规则：调用动作后，必须基于执行结果给出最终中文回复；"
        "除非用户明确要求继续操作，否则不要再输出任何 action JSON。\n"
        "验证规则：修改材质后优先调用 material.verify（截图+识图一步完成）"
        "核对效果；不符合预期时继续调整，确认符合预期后再总结。\n"
        "参数修改请使用完整参数名（如 parameter.baseColor），"
        "避免后缀歧义；未知参数先用 material.graph_query 查看。\n"
        "Material Function 节点（FunctionCall）需要资产 GUID，"
        "AI 不能创建；遇到缺失资产引用时用 material.graph_edit "
        "remove_node 删除坏节点。\n"
        "同类型成品节点（如两个 FresnelRim）共享同一组参数，"
        "这是当前架构限制；需要独立控制时改用 Constant/Parameter "
        "节点连线。\n"
        "材质图思维：值从节点输出流向输入；Surface Output 是终点。"
        "BaseColor/Emissive 是颜色，Roughness/Metallic/Occlusion 是 0~1 标量，"
        "Normal 是切线空间向量；引脚的输入输出类型是硬约束，"
        "类型不匹配就是连错了，按错误信息调整即可。"
        "Float1 输出会广播到所有分量；其他类型必须精确匹配，"
        "或用 Split / MakeFloat2/3/4 构造。"
        "创建 Constant/Parameter 时可传 valueType 与 "
        "valueX/valueY/valueZ/valueW；修改已有节点用 set_node。\n"
        "任务节奏：复杂任务先输出一行计划，格式为 "
        "\"计划: 1. ...; 2. ...\"，再按计划批量执行，最后总结；"
        "简单任务可以省略计划。如果你决定开始执行，"
        "请在输出计划的同时直接给出下一步的整行 action JSON；"
        "只写计划会被要求补发，不会自动执行。\n"
        "不确定某个 action 的参数时，先调用 "
        "{\"action\":\"help\",\"topic\":\"<action名>\"} 查询用法。\n"
        "连线材质节点时，以 add_node 返回的 inputs/outputs 为准；"
        "sourcePin/targetPin 可省略（默认取第一个输出/输入）。\n"
        "复杂材质任务先调用 material.graph_query 查看节点 id 和插槽，"
        "再执行 add_node/connect。创建节点前先调用 material.node_list "
        "查看可用节点；不确定某个节点是做什么时，"
        "再调用 material.node_info 查看它的语义和引脚类型。\n"
        "当前能力清单：\n";
    for (const AI_ASSISTANT::AiCapability& capability :
         AI_ASSISTANT::QueryCapabilities()) {
        prompt += "- " + capability.status + " " + capability.id +
            "：" + capability.summary + "\n";
    }
    prompt +=
        "\n边界：AI 不能直接修改引擎内部状态；不支持的能力必须明确说做不到，"
        "并给出替代方案；未验证的结果不得声称成功。\n"
        "规则：只调用清单中的 action；用户用中文交流时请用中文回复。";
    return prompt;
}

void AiAssistantPanel::AppendMessage(
    const AI_ASSISTANT::AiChatMessage& message) {
    std::lock_guard<std::mutex> lock(messageMutex_);
    messages_.push_back(message);
    if (messages_.size() > 60u) {
        messages_.erase(messages_.begin(), messages_.begin() + 20u);
    }
}

void AiAssistantPanel::AppendReasoning(const std::string& text) {
    const std::string display =
        AI_ASSISTANT::StripActionJsonForDisplay(text);
    if (display.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(reasoningMutex_);
    reasoningLog_.push_back(display);
    if (reasoningLog_.size() > 60u) {
        reasoningLog_.erase(
            reasoningLog_.begin(), reasoningLog_.begin() + 20u);
    }
}

void AiAssistantPanel::RunConversation(const std::string& userText) {
    const AI_ASSISTANT::AiChatConfig config = SnapshotConfig();
    AiContextCallback contextCallback;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        contextCallback = contextCallback_;
    }
    std::vector<AI_ASSISTANT::AiChatMessage> conversation;
    {
        std::lock_guard<std::mutex> lock(messageMutex_);
        conversation = messages_;
    }
    conversation.push_back({ "user", userText });
    int consecutiveFailureRounds = 0;
    int planOnlyRounds = 0;
    int maxToolRounds = kDefaultToolRounds;
    std::string failureSummary;
    for (int turn = 0; turn < maxToolRounds; ++turn) {
        if (stopRequested_.load()) {
            return;
        }
        std::vector<AI_ASSISTANT::AiChatMessage> request = conversation;
        std::string systemContent = BuildSystemPrompt();
        systemContent += "\n预算：本轮第 " +
            std::to_string(turn + 1) + "/" +
            std::to_string(maxToolRounds) + " 轮。";
        if (consecutiveFailureRounds >= 2) {
            systemContent +=
                " 你已连续失败 " +
                std::to_string(consecutiveFailureRounds) +
                " 轮，必须改变策略：先调用诊断动作"
                "（help / material.graph_query / capture.preview / "
                "vision.analyze_image），或明确向用户说明无法继续。";
        }
        systemContent += "\n";
        if (contextCallback) {
            systemContent += "\n\n当前现状：\n" + contextCallback();
        }
        request.insert(
            request.begin(),
            AI_ASSISTANT::AiChatMessage{
                "system", std::move(systemContent) });
        std::string reply;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(streamMutex_);
            streamingPartial_.clear();
            streamingActive_ = true;
        }
        const bool streamOk =
            AI_ASSISTANT::RunChatCompletionsStreaming(
                config,
                request,
                [this](const std::string& delta) {
                    std::lock_guard<std::mutex> lock(streamMutex_);
                    streamingPartial_ += delta;
                    return true;
                },
                reply,
                error);
        {
            std::lock_guard<std::mutex> lock(streamMutex_);
            streamingActive_ = false;
        }
        if (!streamOk) {
            AppendMessage({ "assistant", "（调用失败）" + error });
            return;
        }
        if (!error.empty()) {
            reply += "\n\n" + error;
        }
        const std::vector<std::string> actionJsons =
            AI_ASSISTANT::ExtractAllActionJson(reply);
        if (actionJsons.empty()) {
            if (AI_ASSISTANT::IsPlanOnlyReply(reply) &&
                planOnlyRounds < 2) {
                ++planOnlyRounds;
                AppendReasoning(reply);
                conversation.push_back({ "assistant", reply });
                conversation.push_back(
                    { "user",
                      "你刚才只输出了计划，没有输出任何 action JSON。"
                      "如果确实要开始执行，请在这一轮直接输出下一步的"
                      "整行 action JSON；如果只是回答用户问题，"
                      "请直接给出最终中文回复。" });
                continue;
            }
            AppendMessage({ "assistant", reply });
            {
                std::lock_guard<std::mutex> lock(streamMutex_);
                streamingPartial_.clear();
            }
            return;
        }
        std::vector<std::string> executableActions = actionJsons;
        std::string budgetMessage;
        const auto budgetIt = std::find_if(
            executableActions.begin(), executableActions.end(),
            [](const std::string& actionJson) {
                return AI_ASSISTANT::ExtractJsonField(
                           actionJson, "action") == "budget.request";
            });
        if (budgetIt != executableActions.end()) {
            const std::string roundsText =
                AI_ASSISTANT::ExtractJsonNumberField(
                    *budgetIt, "rounds");
            int requested = 0;
            try {
                requested = std::stoi(roundsText);
            } catch (...) {
            }
            if (requested > kDefaultToolRounds) {
                maxToolRounds =
                    (std::min)(requested, kMaxToolRoundsCeiling);
                budgetMessage =
                    "预算已授予：本轮最多 " +
                    std::to_string(maxToolRounds) + " 轮（申请 " +
                    std::to_string(requested) + "）。";
            } else {
                budgetMessage =
                    "预算申请被忽略：申请轮数 " +
                    std::to_string(requested) + " 不大于默认 " +
                    std::to_string(kDefaultToolRounds) + "。";
            }
            executableActions.erase(budgetIt);
        }
        AppendReasoning(reply);
        conversation.push_back({ "assistant", reply });
        std::string resultText;
        failureSummary.clear();
        size_t successCount = 0u;
        size_t failureCount = 0u;
        if (!budgetMessage.empty()) {
            resultText += budgetMessage + "\n";
        }
        for (const std::string& actionJson : executableActions) {
            const AI_ASSISTANT::AiRequestResult result =
                ExecuteRequest(actionJson);
            if (result.ok) {
                ++successCount;
            } else {
                ++failureCount;
                const std::string actionName =
                    AI_ASSISTANT::ExtractJsonField(
                        actionJson, "action");
                failureSummary +=
                    (failureSummary.empty() ? "" : "\n") +
                    actionName + ": " + result.message;
            }
            resultText +=
                result.ok ? "成功：" : "失败：";
            if (!result.ok && !result.code.empty()) {
                resultText += "[" + result.code + "] ";
            }
            resultText += actionJson + "\n";
            resultText += result.message + "\n";
            size_t detailCount = 0u;
            for (const std::string& detail : result.details) {
                if (detailCount >= 12u) {
                    resultText += "- ...（更多省略）\n";
                    break;
                }
                const std::string trimmed =
                    detail.size() > 160u
                        ? detail.substr(0u, 160u) + "..."
                        : detail;
                resultText += "- " + trimmed + "\n";
                ++detailCount;
            }
        }
        if (failureSummary.size() > 800u) {
            failureSummary =
                failureSummary.substr(0u, 800u) + "...";
        }
        resultText += "\n汇总：" + std::to_string(successCount) +
            " 成功，" + std::to_string(failureCount) + " 失败。";
        conversation.push_back({ "user", resultText });
        if (successCount == 0u && failureCount > 0u) {
            ++consecutiveFailureRounds;
        } else {
            consecutiveFailureRounds = 0;
        }
    }
    std::string stopMessage =
        "已达到工具轮数预算（" +
        std::to_string(maxToolRounds) + " 轮）。";
    if (!failureSummary.empty()) {
        stopMessage += "最近失败原因：\n" + failureSummary;
    }
    stopMessage +=
        "\n你可以申请更多轮数（budget.request），或继续提问。";
    AppendMessage({ "assistant", stopMessage });
    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        streamingPartial_.clear();
    }
}

void AiAssistantPanel::SendCurrentMessage() {
    if (busy_.load()) {
        return;
    }
    std::string text = AI_ASSISTANT::Trim(inputBuffer_);
    if (text.empty()) {
        return;
    }
    AppendMessage({ "user", text });
    inputBuffer_[0] = '\0';
    stopRequested_.store(false);
    busy_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread([this, text = std::move(text)]() {
        try {
            RunConversation(text);
        } catch (...) {
            try {
                AppendMessage({ "assistant", "（内部错误，已停止）" });
            } catch (...) {
            }
        }
        {
            std::lock_guard<std::mutex> lock(streamMutex_);
            streamingActive_ = false;
            streamingPartial_.clear();
        }
        busy_.store(false);
    });
}

void AiAssistantPanel::OpenApiSettingsBuffers() {
    const AI_ASSISTANT::AiChatConfig config = SnapshotConfig();
    std::snprintf(
        apiKeyBuffer_, sizeof(apiKeyBuffer_), "%s",
        config.apiKey.c_str());
    std::snprintf(
        baseUrlBuffer_, sizeof(baseUrlBuffer_), "%s",
        config.baseUrl.c_str());
    std::snprintf(
        modelBuffer_, sizeof(modelBuffer_), "%s",
        config.model.c_str());
}

void AiAssistantPanel::SaveApiSettings() {
    AI_ASSISTANT::AiChatConfig updated{};
    updated.apiKey = AI_ASSISTANT::Trim(apiKeyBuffer_);
    updated.baseUrl = AI_ASSISTANT::Trim(baseUrlBuffer_);
    updated.model = AI_ASSISTANT::Trim(modelBuffer_);
    if (updated.baseUrl.empty()) {
        updated.baseUrl =
            "https://dashscope.aliyuncs.com/compatible-mode/v1";
    }
    if (updated.model.empty()) {
        updated.model = "qwen-plus";
    }
    updated.valid = !updated.apiKey.empty();
    AI_ASSISTANT::SaveAiChatConfig(updated);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        configLoaded_ = AI_ASSISTANT::LoadAiChatConfig(config_);
    }
    OpenApiSettingsBuffers();
}

void AiAssistantPanel::SetSceneActionCallback(
    AiSceneActionCallback callback) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    sceneActionCallback_ = std::move(callback);
}

void AiAssistantPanel::SetContextCallback(
    AiContextCallback callback) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    contextCallback_ = std::move(callback);
}

void AiAssistantPanel::SetBridgeStatus(const std::string& status) {
    bridgeStatus_ = status;
}

AI_ASSISTANT::AiRequestResult AiAssistantPanel::ExecuteRequest(
    std::string_view json) {
    const std::string action =
        AI_ASSISTANT::ExtractJsonField(json, "action");
    AiSceneActionCallback sceneCallback;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        sceneCallback = sceneActionCallback_;
    }
    if (action.rfind("procedural.", 0u) == 0u ||
        action.rfind("scene.", 0u) == 0u ||
        action.rfind("asset.", 0u) == 0u ||
        action.rfind("material.", 0u) == 0u ||
        action.rfind("capture.", 0u) == 0u ||
        action.rfind("action.", 0u) == 0u) {
        if (sceneCallback) {
            return sceneCallback(json);
        }
        AI_ASSISTANT::AiRequestResult result{};
        result.ok = false;
        result.message = "场景执行器未接线：" + action;
        result.details = {
            "supported: query.capabilities",
            "supported: query.material_templates",
            "supported: vision.analyze_image",
        };
        return result;
    }
    return AI_ASSISTANT::SubmitAiRequest(json);
}

AI_ASSISTANT::AiChatConfig AiAssistantPanel::SnapshotConfig() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return config_;
}

void AiAssistantPanel::OpenVisionBuffers() {
    AI_ASSISTANT::AiChatConfig vision{};
    (void)AI_ASSISTANT::LoadAiVisionConfig(vision);
    std::snprintf(
        visionApiKeyBuffer_, sizeof(visionApiKeyBuffer_), "%s",
        vision.apiKey.c_str());
    std::snprintf(
        visionBaseUrlBuffer_, sizeof(visionBaseUrlBuffer_), "%s",
        vision.baseUrl.c_str());
    std::snprintf(
        visionModelBuffer_, sizeof(visionModelBuffer_), "%s",
        vision.model.c_str());
}

void AiAssistantPanel::SaveVisionSettings() {
    AI_ASSISTANT::AiChatConfig updated{};
    updated.apiKey = AI_ASSISTANT::Trim(visionApiKeyBuffer_);
    updated.baseUrl = AI_ASSISTANT::Trim(visionBaseUrlBuffer_);
    updated.model = AI_ASSISTANT::Trim(visionModelBuffer_);
    if (updated.baseUrl.empty()) {
        updated.baseUrl =
            "https://dashscope.aliyuncs.com/compatible-mode/v1";
    }
    if (updated.model.empty()) {
        updated.model = "qwen-vl-plus";
    }
    updated.valid = !updated.apiKey.empty();
    AI_ASSISTANT::SaveAiVisionConfig(updated);
    OpenVisionBuffers();
}

void AiAssistantPanel::ResetVisionSettings() {
    AI_ASSISTANT::ResetAiVisionConfig();
    OpenVisionBuffers();
}

void AiAssistantPanel::Draw(bool& open) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin("AI Assistant", &open, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Capabilities", nullptr, &showCapabilities_);
            ImGui::MenuItem("Raw JSON Action", nullptr, &showRawRequest_);
            if (ImGui::MenuItem("API Settings", nullptr,
                                &showApiSettings_)) {
                if (showApiSettings_) {
                    OpenApiSettingsBuffers();
                    OpenVisionBuffers();
                }
            }
            if (ImGui::MenuItem("Clear Chat")) {
                {
                    std::lock_guard<std::mutex> lock(messageMutex_);
                    messages_.clear();
                }
                {
                    std::lock_guard<std::mutex> lock(reasoningMutex_);
                    reasoningLog_.clear();
                }
                lastMessageCount_ = 0u;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    const bool busy = busy_.load();
    bool configValid = false;
    std::string configModel;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        configValid = config_.valid;
        configModel = config_.model;
    }
    const ImVec4 statusColor = !configLoaded_
        ? ImVec4{ 1.0f, 0.42f, 0.35f, 1.0f }
        : busy
            ? ImVec4{ 1.0f, 0.72f, 0.28f, 1.0f }
            : ImVec4{ 0.28f, 0.82f, 0.48f, 1.0f };
    ImGui::TextColored(statusColor, "%s",
        busy
            ? "● 处理中..."
            : configValid
                ? ("● DashScope · " + configModel).c_str()
                : "● 未配置 API Key（只读模式）");
    if (!bridgeStatus_.empty()) {
        ImGui::TextDisabled("%s", bridgeStatus_.c_str());
    }
    if (ImGui::GetFont() != nullptr &&
        ImGui::GetFont()->FindGlyphNoFallback(
            static_cast<ImWchar>(0x64C7u)) == nullptr) {
        ImGui::TextColored(
            ImVec4{ 1.0f, 0.42f, 0.35f, 1.0f },
            "警告：当前字体缺少部分中文字形（如“擎”），"
            "请确认系统存在 C:/Windows/Fonts/msyh.ttc");
    }

    ImGui::Separator();

    std::string planLine;
    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        planLine =
            AI_ASSISTANT::ExtractPlanLine(streamingPartial_);
    }
    if (!planLine.empty()) {
        ImGui::TextColored(
            ImVec4{ 0.85f, 0.78f, 0.45f, 1.0f },
            "执行计划");
        ImGui::TextWrapped("%s", planLine.c_str());
        ImGui::Spacing();
        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader(
            "思考过程（AI 推理）", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild(
            "##AiReasoningLog", ImVec2(0.0f, 160.0f), true);
        {
            std::vector<std::string> visibleReasoning;
            {
                std::lock_guard<std::mutex> lock(reasoningMutex_);
                visibleReasoning = reasoningLog_;
            }
            for (const std::string& entry : visibleReasoning) {
                ImGui::TextWrapped("%s", entry.c_str());
                ImGui::Spacing();
            }
            std::string partial;
            {
                std::lock_guard<std::mutex> lock(streamMutex_);
                partial = streamingPartial_;
            }
            if (!partial.empty()) {
                const std::string visiblePartial =
                    AI_ASSISTANT::StripActionJsonForDisplay(partial);
                if (!visiblePartial.empty()) {
                    ImGui::TextColored(
                        ImVec4{ 0.65f, 0.72f, 0.80f, 1.0f },
                        "（思考中）");
                    ImGui::TextWrapped("%s", visiblePartial.c_str());
                    ImGui::Spacing();
                    ImGui::SetScrollHereY(1.0f);
                }
            }
        }
        ImGui::EndChild();
        ImGui::Separator();
    }

    ImGui::BeginChild(
        "##AiChatLog", ImVec2(0.0f, -112.0f), true);
    {
        std::vector<AI_ASSISTANT::AiChatMessage> visibleMessages;
        {
            std::lock_guard<std::mutex> lock(messageMutex_);
            visibleMessages = messages_;
        }
        for (const AI_ASSISTANT::AiChatMessage& message : visibleMessages) {
            if (message.role == "user") {
                ImGui::TextColored(
                    ImVec4{ 0.82f, 0.88f, 0.96f, 1.0f },
                    "[You]");
            } else {
                ImGui::TextColored(
                    ImVec4{ 0.45f, 0.80f, 1.00f, 1.0f },
                    "[Assistant]");
            }
            ImGui::TextWrapped("%s", message.content.c_str());
            ImGui::Spacing();
        }
        if (visibleMessages.size() != lastMessageCount_) {
            lastMessageCount_ = visibleMessages.size();
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(busy);
    const bool enterPressed = ImGui::InputTextMultiline(
        "##AiInput",
        inputBuffer_,
        sizeof(inputBuffer_),
        ImVec2(-1.0f, 56.0f),
        ImGuiInputTextFlags_EnterReturnsTrue);
    const bool sendClicked = ImGui::Button("Send");
    if (enterPressed || sendClicked) {
        SendCurrentMessage();
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("working...");
    }

    if (showCapabilities_) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader(
                "Capability Manifest", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable(
                    "##AiCapabilities", 3,
                    ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Status");
                ImGui::TableSetupColumn("ID");
                ImGui::TableSetupColumn("Summary");
                ImGui::TableHeadersRow();
                for (const AI_ASSISTANT::AiCapability& capability :
                     AI_ASSISTANT::QueryCapabilities()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", capability.status.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(capability.id.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s",
                                       capability.summary.c_str());
                }
                ImGui::EndTable();
            }
        }
    }

    if (showApiSettings_) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader(
                "API Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputText(
                "API Key",
                apiKeyBuffer_,
                sizeof(apiKeyBuffer_),
                showApiKey_ ? 0 : ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            ImGui::Checkbox("Show", &showApiKey_);
            ImGui::InputText(
                "Base URL",
                baseUrlBuffer_,
                sizeof(baseUrlBuffer_));
            ImGui::InputText(
                "Model",
                modelBuffer_,
                sizeof(modelBuffer_));
            if (ImGui::Button("Save")) {
                SaveApiSettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset to defaults")) {
                AI_ASSISTANT::ResetAiChatConfig();
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    configLoaded_ =
                        AI_ASSISTANT::LoadAiChatConfig(config_);
                }
                OpenApiSettingsBuffers();
            }
            ImGui::TextDisabled(
                "Saved to %USERPROFILE%\\.hikari-ai-assistant.env");

            ImGui::SeparatorText("Vision API");
            ImGui::InputText(
                "Vision API Key",
                visionApiKeyBuffer_,
                sizeof(visionApiKeyBuffer_),
                showVisionKey_ ? 0 : ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            ImGui::Checkbox("Show", &showVisionKey_);
            ImGui::InputText(
                "Vision Base URL",
                visionBaseUrlBuffer_,
                sizeof(visionBaseUrlBuffer_));
            ImGui::InputText(
                "Vision Model",
                visionModelBuffer_,
                sizeof(visionModelBuffer_));
            if (ImGui::Button("Save Vision")) {
                SaveVisionSettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Vision")) {
                ResetVisionSettings();
            }
            ImGui::TextDisabled(
                "默认读取 vision-skill .env 的 VISION_MODEL；"
                "保存后写入同一引擎配置文件。");
        }
    }

    if (showRawRequest_) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader(
                "Raw JSON Action", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputTextMultiline(
                "##AiRawRequest",
                rawRequestBuffer_,
                sizeof(rawRequestBuffer_),
                ImVec2(-1.0f, 64.0f));
            if (ImGui::Button("Run")) {
                rawResult_ = ExecuteRequest(rawRequestBuffer_);
                hasRawResult_ = true;
            }
            if (hasRawResult_) {
                ImGui::TextColored(
                    rawResult_.ok
                        ? ImVec4{ 0.28f, 0.82f, 0.48f, 1.0f }
                        : ImVec4{ 1.0f, 0.42f, 0.35f, 1.0f },
                    rawResult_.ok ? "OK" : "NOT SUPPORTED");
                ImGui::TextWrapped("%s",
                                   rawResult_.message.c_str());
                for (const std::string& detail : rawResult_.details) {
                    ImGui::BulletText("%s", detail.c_str());
                }
            }
        }
    }

    ImGui::End();
#else
    (void)open;
#endif
}

} // namespace HIKARI::EDITOR
