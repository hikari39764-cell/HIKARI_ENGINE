#include "HIKARI_AI_AssistantPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/AI/HIKARI_AI_VisionClient.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
constexpr int kMaxToolRounds = 8;
} // namespace

namespace AI_ASSISTANT {

namespace {

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char c) {
        return std::isspace(c) == 0;
    };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end());
    return value;
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
        { "capture.preview",
          "Capture the material preview to a BMP file for vision verification.",
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
        { "scene.query",
          "AI-readable scene/object queries through the editor bridge.",
          "planned" },
        { "asset.import",
          "AI-triggered model/texture import through the asset command surface.",
          "planned" },
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
        } else if (topic == "capture.preview") {
            result.details = {
                "{\"action\":\"capture.preview\","
                "\"path\":\"C:/temp/preview.bmp\"}",
                "不传 path 时保存到 %TEMP%/hikari-ai-preview.bmp",
            };
        } else if (topic == "vision.analyze_image") {
            result.details = {
                "{\"action\":\"vision.analyze_image\","
                "\"image\":\"C:/temp/preview.bmp\","
                "\"prompt\":\"描述这张图\"}",
            };
        } else if (topic == "scene.list_objects" ||
                   topic == "scene.snapshot") {
            result.details = {
                "{\"action\":\"scene.list_objects\"}",
                "scene.snapshot 返回同样的对象摘要，"
                "用于修改前后对比",
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
        "不要把它混在普通文字里，也不要在正文里举例 JSON。\n"
        "收尾规则：调用动作后，必须基于执行结果给出最终中文回复；"
        "除非用户明确要求继续操作，否则不要再输出任何 action JSON。\n"
        "任务节奏：复杂任务先输出一行计划，格式为 "
        "\"计划: 1. ...; 2. ...\"，再按计划批量执行，最后总结；"
        "简单任务可以省略计划。\n"
        "不确定某个 action 的参数时，先调用 "
        "{\"action\":\"help\",\"topic\":\"<action名>\"} 查询用法。\n"
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

void AiAssistantPanel::RunConversation(const std::string& userText) {
    std::vector<AI_ASSISTANT::AiChatMessage> conversation;
    {
        std::lock_guard<std::mutex> lock(messageMutex_);
        conversation = messages_;
    }
    conversation.push_back({ "user", userText });
    lastRoundActions_.clear();
    for (int turn = 0; turn < kMaxToolRounds; ++turn) {
        std::vector<AI_ASSISTANT::AiChatMessage> request = conversation;
        std::string systemContent = BuildSystemPrompt();
        if (contextCallback_) {
            systemContent += "\n\n当前现状：\n" + contextCallback_();
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
                config_,
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
        const std::vector<std::string> actionJsons =
            AI_ASSISTANT::ExtractAllActionJson(reply);
        if (actionJsons.empty()) {
            AppendMessage({ "assistant", reply });
            {
                std::lock_guard<std::mutex> lock(streamMutex_);
                streamingPartial_.clear();
            }
            return;
        }
        if (!lastRoundActions_.empty() &&
            std::all_of(
                actionJsons.begin(), actionJsons.end(),
                [this](const std::string& action) {
                    return std::find(
                        lastRoundActions_.begin(),
                        lastRoundActions_.end(),
                        action) != lastRoundActions_.end();
                })) {
            AppendMessage(
                { "assistant",
                  "检测到连续重复的动作调用，已停止以避免循环；"
                  "请基于当前结果继续说明，或让我手动操作。" });
            {
                std::lock_guard<std::mutex> lock(streamMutex_);
                streamingPartial_.clear();
            }
            return;
        }
        lastRoundActions_ = actionJsons;
        conversation.push_back({ "assistant", reply });
        std::string resultText;
        size_t successCount = 0u;
        size_t failureCount = 0u;
        for (const std::string& actionJson : actionJsons) {
            const AI_ASSISTANT::AiRequestResult result =
                ExecuteRequest(actionJson);
            if (result.ok) {
                ++successCount;
            } else {
                ++failureCount;
            }
            resultText +=
                result.ok ? "成功：" : "失败：";
            resultText += actionJson + "\n";
            resultText += result.message + "\n";
            for (const std::string& detail : result.details) {
                resultText += "- " + detail + "\n";
            }
        }
        resultText += "\n汇总：" + std::to_string(successCount) +
            " 成功，" + std::to_string(failureCount) + " 失败。";
        conversation.push_back({ "user", resultText });
    }
    AppendMessage({ "assistant", "（已达到最大工具调用轮数，请继续提问）" });
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
    busy_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread([this, text = std::move(text)]() {
        try {
            RunConversation(text);
        } catch (...) {
            AppendMessage({ "assistant", "（内部错误，已停止）" });
            {
                std::lock_guard<std::mutex> lock(streamMutex_);
                streamingActive_ = false;
                streamingPartial_.clear();
            }
        }
        busy_.store(false);
    });
}

void AiAssistantPanel::OpenApiSettingsBuffers() {
    std::snprintf(
        apiKeyBuffer_, sizeof(apiKeyBuffer_), "%s",
        config_.apiKey.c_str());
    std::snprintf(
        baseUrlBuffer_, sizeof(baseUrlBuffer_), "%s",
        config_.baseUrl.c_str());
    std::snprintf(
        modelBuffer_, sizeof(modelBuffer_), "%s",
        config_.model.c_str());
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
    configLoaded_ = AI_ASSISTANT::LoadAiChatConfig(config_);
    OpenApiSettingsBuffers();
}

void AiAssistantPanel::SetSceneActionCallback(
    AiSceneActionCallback callback) {
    sceneActionCallback_ = std::move(callback);
}

void AiAssistantPanel::SetContextCallback(
    AiContextCallback callback) {
    contextCallback_ = std::move(callback);
}

AI_ASSISTANT::AiRequestResult AiAssistantPanel::ExecuteRequest(
    std::string_view json) {
    const std::string action =
        AI_ASSISTANT::ExtractJsonField(json, "action");
    if (action.rfind("procedural.", 0u) == 0u ||
        action.rfind("scene.", 0u) == 0u ||
        action.rfind("asset.", 0u) == 0u ||
        action.rfind("material.", 0u) == 0u ||
        action.rfind("capture.", 0u) == 0u ||
        action.rfind("action.", 0u) == 0u) {
        if (sceneActionCallback_) {
            return sceneActionCallback_(json);
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
                std::lock_guard<std::mutex> lock(messageMutex_);
                messages_.clear();
                lastMessageCount_ = 0u;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    const bool busy = busy_.load();
    const ImVec4 statusColor = !configLoaded_
        ? ImVec4{ 1.0f, 0.42f, 0.35f, 1.0f }
        : busy
            ? ImVec4{ 1.0f, 0.72f, 0.28f, 1.0f }
            : ImVec4{ 0.28f, 0.82f, 0.48f, 1.0f };
    ImGui::TextColored(statusColor, "%s",
        busy
            ? "● 处理中..."
            : config_.valid
                ? ("● DashScope · " + config_.model).c_str()
                : "● 未配置 API Key（只读模式）");
    if (ImGui::GetFont() != nullptr &&
        ImGui::GetFont()->FindGlyphNoFallback(
            static_cast<ImWchar>(0x64C7u)) == nullptr) {
        ImGui::TextColored(
            ImVec4{ 1.0f, 0.42f, 0.35f, 1.0f },
            "警告：当前字体缺少部分中文字形（如“擎”），"
            "请确认系统存在 C:/Windows/Fonts/msyh.ttc");
    }

    ImGui::Separator();

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
        std::string partial;
        {
            std::lock_guard<std::mutex> lock(streamMutex_);
            partial = streamingPartial_;
        }
        if (!partial.empty()) {
            ImGui::TextColored(
                ImVec4{ 0.45f, 0.80f, 1.00f, 1.0f },
                "[Assistant]");
            ImGui::TextWrapped("%s", partial.c_str());
            ImGui::Spacing();
        }
        if (visibleMessages.size() != lastMessageCount_ ||
            !partial.empty()) {
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
                configLoaded_ = AI_ASSISTANT::LoadAiChatConfig(config_);
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
