#include "HIKARI_AI_AssistantPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/AI/HIKARI_AI_VisionClient.h"

#include "Core/Text/HIKARI_Trim.h"
#include "Core/Text/HIKARI_Localization.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
constexpr int kDefaultToolRounds = 40;
constexpr int kMaxToolRoundsCeiling = 120;
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

const std::vector<AiCapability>& BuiltInCapabilities() {
    static const std::vector<AiCapability> capabilities{
        { "chat.converse",
          "Conversational replies via the configured DashScope-compatible chat API; action JSON executes through SubmitAiRequest.",
          "stable" },
        { "vision.analyze_image",
          "Analyze a local image via the configured vision API; defaults to the vision-skill DashScope model.",
          "stable" },
        { "budget.request",
          "Request a higher tool-round budget for the current conversation.",
          "stable" },
        { "help",
          "Query detailed usage and examples for an action.",
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

AiRequestResult SubmitAiRequest(
    std::string_view requestJson,
    const std::vector<AiCapability>& capabilities) {
    const std::string action = Trim(ExtractAction(requestJson));
    if (action == "list_capabilities") {
        AiRequestResult result{};
        result.ok = true;
        result.message = "当前 AI 可访问能力清单如下（status: stable/planned/unsupported）。";
        for (const AiCapability& capability : capabilities) {
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
        } else if (topic == "material.add_node") {
            result.details = {
                "{\"action\":\"material.add_node\","
                "\"nodeType\":\"Constant\",\"name\":\"RimColor\","
                "\"valueType\":\"Float4\",\"valueX\":1,\"valueY\":1,"
                "\"valueZ\":1,\"valueW\":1}",
                "nodeType 支持 stable name / node.xxx / 成品名；"
                "不传 manualPosition 时按分类自动排布",
                "Material Function 节点必须由人类在编辑器的 "
                "Material Functions 菜单添加",
            };
        } else if (topic == "material.open_model" ||
                   topic == "material.open_asset" ||
                   topic == "material.list_assets" ||
                   topic == "collision.open_model" ||
                   topic == "asm.new_asset" ||
                   topic == "asm.open_asset") {
            result.details = {
                "后台打开/新建文档，不会切换人类当前工作区："
                "{\"action\":\"material.open_model\","
                "\"modelGuid\":\"<guid>\"}",
                "先用 asset.list_models / material.list_assets "
                "查询资产 guid",
                "当前文档有未保存修改时返回 PENDING_CONFIRMATION，"
                "需要用户先保存或确认",
                "打开后即可在后台继续材质/碰撞/状态机编辑，"
                "人类切到对应工作区时看到同样的结果",
            };
        } else if (topic == "model.list_slots") {
            result.details = {
                "{\"action\":\"material.open_model\","
                "\"modelGuid\":\"<guid>\"}",
                "{\"action\":\"model.list_slots\"}",
                "返回每个材质槽的 materialIndex / slotId / 表面数 / "
                "来源与已绑定的 override 材质",
            };
        } else if (topic == "model.bind_material" ||
                   topic == "model.restore_imported_material") {
            result.details = {
                "替换人物/模型材质工作流：",
                "1. material.list_assets 查询可用材质 guid",
                "2. material.open_model 后台打开目标模型",
                "3. model.list_slots 查询槽位 materialIndex",
                "4. {\"action\":\"model.bind_material\","
                "\"materialIndex\":0,"
                "\"materialAssetGuid\":\"<guid>\"}",
                "恢复导入材质：{\"action\":"
                "\"model.restore_imported_material\","
                "\"materialIndex\":0}",
                "绑定修改进入材质工作区历史，可用 material.save "
                "保存绑定资产持久化",
            };
        } else if (topic == "material.duplicate_selected" ||
                   topic == "material.delete_selected" ||
                   topic == "material.delete_connection" ||
                   topic == "material.group_selected" ||
                   topic == "material.ungroup_selected" ||
                   topic == "material.arrange_selected") {
            result.details = {
                "操作当前材质图中选中的节点/连线；"
                "先调用 material.graph_query 或与用户确认选中状态",
                "删除/分组/整理都是可撤销的文档操作",
            };
        } else if (topic == "material.frame_selection" ||
                   topic == "material.frame_all" ||
                   topic == "material.frame_preview" ||
                   topic == "material.collapse_selected" ||
                   topic == "material.expand_selected" ||
                   topic == "material.collapse_all" ||
                   topic == "material.expand_all") {
            result.details = {
                "视图/会话操作：frame 调整画布或预览相机，"
                "collapse/expand 切换节点折叠状态",
                "配合 capture.preview / material.verify 截图验证效果",
            };
        } else if (topic == "material.verify") {
            result.details = {
                "{\"action\":\"material.verify\"}",
                "先截图再调用视觉 API 分析，返回描述与是否符合预期",
            };
        } else if (topic == "budget.request") {
            result.details = {
                "{\"action\":\"budget.request\","
                "\"rounds\":80,\"reason\":\"逐节点检查需要更多轮\"}",
                "默认 40 轮，上限 120；授予后每轮会返回剩余预算",
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
        } else if (topic == "action.undo_last") {
            result.details = {
                "撤销活动工作区最近一次编辑：材质/碰撞/状态机/过场"
                "各自撤销自己的工作区历史，场景工作区撤销场景历史",
                "AI 与人类的编辑共用同一份撤销历史，"
                "最近一次人类编辑也会被撤销",
            };
        } else if (topic == "asset.import" ||
                   topic == "asset.import_status") {
            result.details = {
                "{\"action\":\"asset.import\","
                "\"path\":\"C:/model.glb\"}",
                "asset.import_status 返回排队导入的进度",
            };
        } else if (topic == "collision.add_shape") {
            result.details = {
                "{\"action\":\"collision.add_shape\",\"type\":\"box\"}",
                "type: box/sphere/capsule/convex_hull/triangle_mesh；"
                "需要在模型碰撞工作区打开模型",
            };
        } else if (topic == "collision.generate") {
            result.details = {
                "{\"action\":\"collision.generate\"}",
                "完成后用 collision.apply_generation_draft 应用，"
                "或用 collision.discard_generation_draft 丢弃",
            };
        } else if (topic == "asm.query") {
            result.details = {
                "{\"action\":\"asm.query\"}",
                "返回当前状态机的状态/参数/过渡 id，"
                "后续操作都基于这里的 id 或名称",
            };
        } else if (topic == "asm.add_transition") {
            result.details = {
                "{\"action\":\"asm.query\"}",
                "{\"action\":\"asm.add_transition\","
                "\"source\":\"Idle\",\"target\":\"Run\"}",
                "source/target 支持状态 id 或名称",
            };
        } else if (topic == "asm.set_state_property") {
            result.details = {
                "{\"action\":\"asm.set_state_property\","
                "\"state\":\"Idle\",\"name\":\"待机\",\"speed\":1.0}",
                "可选字段：name/speed/loop",
            };
        } else if (topic == "asm.add_state" ||
                   topic == "asm.delete_state" ||
                   topic == "asm.delete_selected" ||
                   topic == "asm.add_parameter" ||
                   topic == "asm.delete_parameter" ||
                   topic == "asm.delete_transition" ||
                   topic == "asm.save" ||
                   topic == "asm.undo" ||
                   topic == "asm.redo") {
            result.details = {
                "在动画状态机工作区打开文档后可用；"
                "先用 asm.query 查询 id/名称再引用",
            };
        } else if (topic == "collision.save" ||
                   topic == "collision.undo" ||
                   topic == "collision.redo" ||
                   topic == "collision.delete_selected" ||
                   topic == "collision.duplicate_selected" ||
                   topic == "collision.set_selected_enabled" ||
                   topic == "collision.set_selected_hidden" ||
                   topic == "collision.set_selected_locked" ||
                   topic == "collision.show_all" ||
                   topic == "collision.hide_unselected" ||
                   topic == "collision.apply_generation_draft" ||
                   topic == "collision.discard_generation_draft") {
            result.details = {
                "在模型碰撞工作区打开模型后可用；选中形状后可用 "
                "collision.delete_selected/duplicate_selected/"
                "set_selected_enabled/set_selected_hidden/set_selected_locked",
            };
        } else if (topic == "cinematics.list_assets") {
            result.details = {
                "{\"action\":\"cinematics.list_assets\"}",
                "返回 Sequence 资产的 guid/名称/路径，"
                "open_asset 需要其中的 guid",
            };
        } else if (topic == "cinematics.open_asset") {
            result.details = {
                "{\"action\":\"cinematics.list_assets\"}",
                "{\"action\":\"cinematics.open_asset\","
                "\"assetGuid\":\"<guid>\"}",
                "打开前先查询当前文档状态："
                "{\"action\":\"cinematics.query\"}",
            };
        } else if (topic == "cinematics.query") {
            result.details = {
                "{\"action\":\"cinematics.query\"}",
                "返回文档来源/名称/脏状态/撤销重做能力，"
                "以及序列名称、时长和镜头数",
            };
        } else if (topic == "cinematics.new_asset" ||
                   topic == "cinematics.revert" ||
                   topic == "cinematics.close_to_embedded") {
            result.details = {
                "cinematics.new_asset 创建新的未保存序列资产；"
                "cinematics.revert 把打开的序列资产恢复到已保存版本；"
                "cinematics.close_to_embedded 返回场景内嵌时间线",
                "有未保存修改时会返回 PENDING_CONFIRMATION，"
                "需要用户先在编辑器确认框中选择保存或丢弃",
            };
        } else if (topic == "cinematics.save" ||
                   topic == "cinematics.undo" ||
                   topic == "cinematics.redo") {
            result.details = {
                "仅对已打开的序列资产生效；"
                "Scene Cinematics（场景内嵌时间线）请使用 scene.save",
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
    Stop();
}

void AiAssistantPanel::Stop() {
    stopRequested_.store(true);
    actionCv_.notify_all();
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
         Capabilities()) {
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
    if (messages_.size() > 120u) {
        const size_t keepHead = 20u;
        const size_t keepTail = 80u;
        messages_.erase(
            messages_.begin() +
                static_cast<std::ptrdiff_t>(keepHead),
            messages_.begin() +
                static_cast<std::ptrdiff_t>(
                    messages_.size() - keepTail));
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
        const int remainingRounds = maxToolRounds - turn - 1;
        if (remainingRounds <= 8 &&
            maxToolRounds < kMaxToolRoundsCeiling) {
            systemContent +=
                " 剩余预算不足（" +
                std::to_string(remainingRounds) +
                " 轮）。如果任务仍需继续，"
                "请在这一轮直接输出 {\"action\":\"budget.request\","
                "\"rounds\":N} 申请更多轮数（上限 " +
                std::to_string(kMaxToolRoundsCeiling) + "）。";
        }
        if (consecutiveFailureRounds >= 2) {
            systemContent +=
                " 你已连续失败 " +
                std::to_string(consecutiveFailureRounds) +
                " 轮，必须改变策略：先调用诊断动作"
                "（help / material.graph_query / capture.preview / "
                "vision.analyze_image），或明确向用户说明无法继续。";
        }
        systemContent += "\n";
        const std::string contextSnapshot = SnapshotContext();
        if (!contextSnapshot.empty()) {
            systemContent += "\n\n当前现状：\n" + contextSnapshot;
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
                planOnlyRounds < 3) {
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
                if (detailCount >= 32u) {
                    resultText += "- ...（更多省略）\n";
                    break;
                }
                const std::string trimmed =
                    detail.size() > 320u
                        ? detail.substr(0u, 320u) + "..."
                        : detail;
                resultText += "- " + trimmed + "\n";
                ++detailCount;
            }
        }
        if (failureSummary.size() > 2000u) {
            failureSummary =
                failureSummary.substr(0u, 2000u) + "...";
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
    if (worker_.joinable()) {
        stopRequested_.store(true);
        actionCv_.notify_all();
        worker_.join();
    }
    stopRequested_.store(false);
    busy_.store(true);
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

void AiAssistantPanel::SetContextSnapshot(std::string snapshot) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    contextSnapshot_ = std::move(snapshot);
}

void AiAssistantPanel::SetCapabilityProvider(
    AiCapabilityProvider callback) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    capabilityProvider_ = std::move(callback);
}

std::vector<AI_ASSISTANT::AiCapability> AiAssistantPanel::Capabilities()
    const {
    AiCapabilityProvider provider;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        provider = capabilityProvider_;
    }
    if (provider) {
        return provider();
    }
    return AI_ASSISTANT::BuiltInCapabilities();
}

void AiAssistantPanel::SetBridgeStatus(const std::string& status) {
    bridgeStatus_ = status;
}

AI_ASSISTANT::AiRequestResult AiAssistantPanel::ExecuteRequest(
    std::string_view json) {
    if (stopRequested_.load()) {
        AI_ASSISTANT::AiRequestResult result{};
        result.code = "ACTION_CANCELED";
        result.message = "动作已被取消（编辑器正在关闭）";
        return result;
    }

    const std::string action =
        AI_ASSISTANT::ExtractJsonField(json, "action");
    // Panel-local actions are handled here; every other action is routed to
    // the unified operation registry through the scene callback.
    const bool panelLocalAction =
        action == "list_capabilities" ||
        action == "list_material_templates" ||
        action == "vision.analyze_image" ||
        action == "help" ||
        action == "budget.request" ||
        action == "create_material";
    if (panelLocalAction) {
        return AI_ASSISTANT::SubmitAiRequest(json, Capabilities());
    }

    bool workerExecution = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        workerExecution = workerExecutionClassifier_
            ? workerExecutionClassifier_(action)
            : false;
    }
    if (workerExecution) {
        return ExecuteRequestNow(json);
    }

    std::thread::id mainThreadId{};
    {
        std::lock_guard<std::mutex> lock(actionMutex_);
        mainThreadId = mainThreadId_;
    }
    if (std::this_thread::get_id() == mainThreadId) {
        return ExecuteRequestNow(json);
    }

    auto request = std::make_shared<PendingAiAction>();
    request->json = std::string(json);
    {
        std::lock_guard<std::mutex> lock(actionMutex_);
        pendingActions_.push_back(request);
    }
    actionCv_.notify_one();
    std::unique_lock<std::mutex> waitLock(actionMutex_);
    actionCv_.wait(
        waitLock,
        [this, &request]() {
            return request->result.has_value() ||
                stopRequested_.load();
        });
    if (!request->result.has_value()) {
        AI_ASSISTANT::AiRequestResult result{};
        result.ok = false;
        result.code = "ACTION_CANCELED";
        result.message = "动作已被取消（用户停止）";
        return result;
    }
    return *request->result;
}

AI_ASSISTANT::AiRequestResult AiAssistantPanel::ExecuteRequestNow(
    std::string_view json) {
    const std::string action =
        AI_ASSISTANT::ExtractJsonField(json, "action");
    AiSceneActionCallback sceneCallback;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        sceneCallback = sceneActionCallback_;
    }
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

void AiAssistantPanel::PumpActions() {
    std::vector<std::shared_ptr<PendingAiAction>> actions;
    {
        std::lock_guard<std::mutex> lock(actionMutex_);
        mainThreadId_ = std::this_thread::get_id();
        if (pendingActions_.empty()) {
            return;
        }
        actions.swap(pendingActions_);
    }
    for (const std::shared_ptr<PendingAiAction>& request : actions) {
        AI_ASSISTANT::AiRequestResult result =
            ExecuteRequestNow(request->json);
        {
            std::lock_guard<std::mutex> lock(actionMutex_);
            request->result = std::move(result);
        }
    }
    actionCv_.notify_all();
}

void AiAssistantPanel::SetWorkerExecutionClassifier(
    AiWorkerExecutionClassifier callback) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    workerExecutionClassifier_ = std::move(callback);
}

AI_ASSISTANT::AiChatConfig AiAssistantPanel::SnapshotConfig() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return config_;
}

std::string AiAssistantPanel::SnapshotContext() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return contextSnapshot_;
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
    if (!ImGui::Begin(
            TEXT::LocalizeWindow(
                "ai.title", "AI Assistant").c_str(),
            &open, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem(
                TEXT::Localize("ai.menu.capabilities").c_str(),
                nullptr, &showCapabilities_);
            ImGui::MenuItem(
                TEXT::Localize("ai.menu.rawJson").c_str(),
                nullptr, &showRawRequest_);
            if (ImGui::MenuItem(
                    TEXT::Localize("ai.menu.apiSettings").c_str(),
                    nullptr,
                                &showApiSettings_)) {
                if (showApiSettings_) {
                    OpenApiSettingsBuffers();
                    OpenVisionBuffers();
                }
            }
            if (ImGui::MenuItem(
                    TEXT::Localize("ai.menu.clearChat").c_str())) {
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
            ? TEXT::Localize("ai.status.processing").c_str()
            : configValid
                ? ("● DashScope · " + configModel).c_str()
                : TEXT::Localize("ai.status.noApi").c_str());
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
            TEXT::Localize("ai.plan").c_str());
        ImGui::TextWrapped("%s", planLine.c_str());
        ImGui::Spacing();
        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader(
            TEXT::Localize("ai.reasoning").c_str(),
            ImGuiTreeNodeFlags_DefaultOpen)) {
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
                        TEXT::Localize("ai.status.thinking").c_str());
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
                    TEXT::Localize("ai.chat.you").c_str());
            } else {
                ImGui::TextColored(
                    ImVec4{ 0.45f, 0.80f, 1.00f, 1.0f },
                    TEXT::Localize("ai.chat.assistant").c_str());
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
    const bool sendClicked = ImGui::Button(
        TEXT::Localize("ai.send").c_str());
    if (enterPressed || sendClicked) {
        SendCurrentMessage();
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled(
            TEXT::Localize("ai.working").c_str());
    }

    if (showCapabilities_) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader(
                TEXT::Localize("ai.capabilities").c_str(),
                ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable(
                    "##AiCapabilities", 3,
                    ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(
                    TEXT::Localize("ai.capabilities.status").c_str());
                ImGui::TableSetupColumn(
                    TEXT::Localize("ai.capabilities.id").c_str());
                ImGui::TableSetupColumn(
                    TEXT::Localize("ai.capabilities.summary").c_str());
                ImGui::TableHeadersRow();
                for (const AI_ASSISTANT::AiCapability& capability :
                     Capabilities()) {
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
                TEXT::Localize("ai.menu.apiSettings").c_str(),
                ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputText(
                TEXT::Localize("ai.api.key").c_str(),
                apiKeyBuffer_,
                sizeof(apiKeyBuffer_),
                showApiKey_ ? 0 : ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            ImGui::Checkbox(
                TEXT::Localize("ai.api.show").c_str(), &showApiKey_);
            ImGui::InputText(
                TEXT::Localize("ai.api.baseUrl").c_str(),
                baseUrlBuffer_,
                sizeof(baseUrlBuffer_));
            ImGui::InputText(
                TEXT::Localize("ai.api.model").c_str(),
                modelBuffer_,
                sizeof(modelBuffer_));
            if (ImGui::Button(
                    TEXT::Localize("ai.api.save").c_str())) {
                SaveApiSettings();
            }
            ImGui::SameLine();
            if (ImGui::Button(
                    TEXT::Localize("ai.api.reset").c_str())) {
                AI_ASSISTANT::ResetAiChatConfig();
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    configLoaded_ =
                        AI_ASSISTANT::LoadAiChatConfig(config_);
                }
                OpenApiSettingsBuffers();
            }
            ImGui::TextDisabled(
                TEXT::Localize("ai.api.savedTo").c_str());

            ImGui::SeparatorText(
                TEXT::Localize("ai.api.vision").c_str());
            ImGui::InputText(
                TEXT::Localize("ai.api.visionKey").c_str(),
                visionApiKeyBuffer_,
                sizeof(visionApiKeyBuffer_),
                showVisionKey_ ? 0 : ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            ImGui::Checkbox(
                TEXT::Localize("ai.api.show").c_str(), &showVisionKey_);
            ImGui::InputText(
                TEXT::Localize("ai.api.visionBaseUrl").c_str(),
                visionBaseUrlBuffer_,
                sizeof(visionBaseUrlBuffer_));
            ImGui::InputText(
                TEXT::Localize("ai.api.visionModel").c_str(),
                visionModelBuffer_,
                sizeof(visionModelBuffer_));
            if (ImGui::Button(
                    TEXT::Localize("ai.api.saveVision").c_str())) {
                SaveVisionSettings();
            }
            ImGui::SameLine();
            if (ImGui::Button(
                    TEXT::Localize("ai.api.resetVision").c_str())) {
                ResetVisionSettings();
            }
            ImGui::TextDisabled(
                TEXT::Localize("ai.api.visionDefault").c_str());
        }
    }

    if (showRawRequest_) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader(
                TEXT::Localize("ai.raw.title").c_str(),
                ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputTextMultiline(
                "##AiRawRequest",
                rawRequestBuffer_,
                sizeof(rawRequestBuffer_),
                ImVec2(-1.0f, 64.0f));
            if (ImGui::Button(
                    TEXT::Localize("ai.raw.run").c_str())) {
                rawResult_ = ExecuteRequest(rawRequestBuffer_);
                hasRawResult_ = true;
            }
            if (hasRawResult_) {
                ImGui::TextColored(
                    rawResult_.ok
                        ? ImVec4{ 0.28f, 0.82f, 0.48f, 1.0f }
                        : ImVec4{ 1.0f, 0.42f, 0.35f, 1.0f },
                    rawResult_.ok
                        ? TEXT::Localize("ai.raw.ok").c_str()
                        : TEXT::Localize("ai.raw.fail").c_str());
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
