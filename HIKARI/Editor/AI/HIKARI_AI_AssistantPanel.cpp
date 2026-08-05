#include "HIKARI_AI_AssistantPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
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
        { "material.edit_parameter",
          "AI-driven parameter edits through the editor command surface.",
          "planned" },
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
        "你是 HIKARI 引擎的 AI 编辑助手。你可以通过单独输出一行 JSON 调用引擎能力，"
        "例如 {\"action\":\"list_capabilities\"}。\n"
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
    for (int turn = 0; turn < 4; ++turn) {
        std::vector<AI_ASSISTANT::AiChatMessage> request = conversation;
        request.insert(
            request.begin(),
            AI_ASSISTANT::AiChatMessage{ "system", BuildSystemPrompt() });
        std::string reply;
        std::string error;
        if (!AI_ASSISTANT::RunChatCompletions(
                config_, request, reply, error)) {
            AppendMessage({ "assistant", "（调用失败）" + error });
            return;
        }
        const std::string actionJson =
            AI_ASSISTANT::ExtractActionJson(reply);
        if (actionJson.empty()) {
            AppendMessage({ "assistant", reply });
            return;
        }
        const AI_ASSISTANT::AiRequestResult result =
            AI_ASSISTANT::SubmitAiRequest(actionJson);
        conversation.push_back({ "assistant", actionJson });
        std::string resultText =
            result.ok ? "ACTION OK：" : "ACTION NOT SUPPORTED：";
        resultText += result.message;
        for (const std::string& detail : result.details) {
            resultText += "\n- " + detail;
        }
        conversation.push_back({ "user", resultText });
    }
    AppendMessage({ "assistant", "（已达到最大工具调用轮数，请继续提问）" });
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
        RunConversation(text);
        busy_.store(false);
    });
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
                rawResult_ = AI_ASSISTANT::SubmitAiRequest(rawRequestBuffer_);
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
