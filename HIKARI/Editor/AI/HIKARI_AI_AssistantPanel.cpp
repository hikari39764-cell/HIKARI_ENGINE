#include "HIKARI_AI_AssistantPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace AI_ASSISTANT {

namespace {

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

} // namespace

const std::vector<AiCapability>& QueryCapabilities() {
    static const std::vector<AiCapability> capabilities{
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

void AiAssistantPanel::Draw(bool& open) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin("AI Assistant", &open)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Calling method:");
    ImGui::TextWrapped(
        "Send {\"action\":\"...\"} JSON here, or through the future local "
        "bridge. Unknown actions are rejected with an explicit boundary "
        "message; the assistant never claims unsupported work.");

    ImGui::SeparatorText("Capability Manifest");
    for (const AI_ASSISTANT::AiCapability& capability :
         AI_ASSISTANT::QueryCapabilities()) {
        ImVec4 color{ 0.62f, 0.76f, 0.90f, 1.0f };
        if (capability.status == "stable") {
            color = ImVec4{ 0.28f, 0.82f, 0.48f, 1.0f };
        } else if (capability.status == "planned") {
            color = ImVec4{ 1.0f, 0.72f, 0.28f, 1.0f };
        } else if (capability.status == "unsupported") {
            color = ImVec4{ 1.0f, 0.42f, 0.35f, 1.0f };
        }
        ImGui::TextColored(color, "[%s] %s", capability.status.c_str(),
                           capability.id.c_str());
        ImGui::TextWrapped("  %s", capability.summary.c_str());
    }

    ImGui::SeparatorText("Request");
    ImGui::InputTextMultiline(
        "##AiRequest", requestBuffer_, sizeof(requestBuffer_),
        ImVec2(-1.0f, 96.0f));
    if (ImGui::Button("Run")) {
        lastRequest_ = requestBuffer_;
        lastResult_ = AI_ASSISTANT::SubmitAiRequest(lastRequest_);
        hasResult_ = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Insert: list_capabilities")) {
        std::snprintf(requestBuffer_, sizeof(requestBuffer_),
                      "{\"action\":\"list_capabilities\"}");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Insert: list_material_templates")) {
        std::snprintf(requestBuffer_, sizeof(requestBuffer_),
                      "{\"action\":\"list_material_templates\"}");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Insert: create_material")) {
        std::snprintf(requestBuffer_, sizeof(requestBuffer_),
                      "{\"action\":\"create_material\","
                      "\"template\":\"GenericToon\"}");
    }

    if (hasResult_) {
        ImGui::SeparatorText("Result");
        ImGui::TextColored(
            lastResult_.ok
                ? ImVec4{ 0.28f, 0.82f, 0.48f, 1.0f }
                : ImVec4{ 1.0f, 0.42f, 0.35f, 1.0f },
            lastResult_.ok ? "OK" : "NOT SUPPORTED");
        ImGui::TextWrapped("%s", lastResult_.message.c_str());
        for (const std::string& detail : lastResult_.details) {
            ImGui::BulletText("%s", detail.c_str());
        }
    }

    ImGui::End();
#else
    (void)open;
#endif
}

} // namespace HIKARI::EDITOR
