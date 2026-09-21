#include "HIKARI_AI_AssistantSession.h"
#include "HIKARI_AI_MessageText.h"

namespace HIKARI::EDITOR {
AI_ASSISTANT::AiChatMessage AiAssistantSession::ExecuteConversationAction(const std::string& actionJson) {
    const auto execute = [this](const std::string& json) {
        auto parameters = nlohmann::json::parse(json);
        parameters["_backgroundTask"] = true;
        return ExecuteRequest(parameters.dump());
    };
    const auto actionName = AI_ASSISTANT::ExtractJsonField(actionJson,"action");
    const auto sessionId = AI_ASSISTANT::ExtractJsonField(actionJson,"sessionId");
    SetActivity("executing", actionName, sessionId);
    const auto started = std::chrono::steady_clock::now();
    AI_ASSISTANT::AiRequestResult result =
        execute(actionJson);
    // 競合待ちはローカルで行い、モデルへの再問い合わせを増やさない。
    const auto editDeadline = started + std::chrono::minutes(2);
    while (!result.ok && result.code == "DOCUMENT_BUSY" && !stopRequested_.load() &&
           std::chrono::steady_clock::now() < editDeadline) {
        SetActivity("waiting_edit", actionName, sessionId);
        std::unique_lock lock(actionMutex_);
        actionCv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return stopRequested_.load(); });
        lock.unlock();
        if (!stopRequested_.load()) result = execute(actionJson);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (result.ok && result.data.value("pending", false) && result.data.contains("poll")) {
        if (stopRequested_.load()) return {"tool", "Cancelled", {}, {{"ok", false}, {"code", "CANCELLED"}}};
        SetActivity(result.data.value("stage",std::string("render")), actionName, sessionId);
        if (std::chrono::steady_clock::now() >= deadline) {
            result.ok = false;
            result.code = "PREVIEW_TIMEOUT";
            result.message = "预览超时，停在" + result.data.value("stage", std::string("render")) + "阶段。尚未取得图片。";
            if (result.data.contains("renderFailure")) result.message += " " + result.data["renderFailure"].get<std::string>();
            result.data.erase("poll"); result.data["pending"] = false;
            if (!sessionId.empty()) (void)execute(nlohmann::json{{"action","authoring.cancel_preview"},{"sessionId",sessionId}}.dump());
            break;
        }
        const std::string poll = result.data["poll"].dump();
        std::unique_lock lock(actionMutex_);
        actionCv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return stopRequested_.load(); });
        lock.unlock();
        result = execute(poll);
    }
    if (stopRequested_.load()) return {"tool", "Cancelled", {}, {{"ok", false}, {"code", "CANCELLED"}}};
    AI_ASSISTANT::AiChatMessage visibleResult{"tool", result.message};
    if (result.ok && result.data.contains("images") && result.data["images"].is_array()) {
        for (const auto& item : result.data["images"]) {
            const std::string path = item.value("path", std::string{});
            std::string uri, imageError;
            if (!AI_ASSISTANT::ReadAiImageDataUri(path, uri, imageError)) {
                result.ok = false;
                result.code = "IMAGE_READ_FAILED";
                result.message = imageError;
                break;
            }
            visibleResult.images.push_back({path, item.value("label", std::string{})});
        }
    }
    visibleResult.data = result.data;
    if (!visibleResult.data.is_object()) visibleResult.data=nlohmann::json::object();
    visibleResult.data["action"] = AI_ASSISTANT::ExtractJsonField(actionJson,"action");
    visibleResult.data["ok"] = result.ok;
    visibleResult.data["code"] = result.code;
    visibleResult.data["request"] = nlohmann::json::parse(actionJson);
    if (!result.details.empty()) visibleResult.data["details"] = result.details;
    if (!result.hints.empty()) visibleResult.data["hints"] = result.hints;
    if (!result.ok) {
        visibleResult.data["batchStopped"] = true;
        if (result.code == "PREVIEW_TIMEOUT" || result.code == "CAPTURE_FAILED" || result.code == "PREVIEW_FAILED" || result.code == "IMAGE_READ_FAILED") {
            visibleResult.data["verificationPending"] = true;
            visibleResult.data["hints"] = {"Preview transport failed. Preserve content; retry preview or continue structural work. Visual verification is still pending."};
        }
    }
    if (!sessionId.empty()) visibleResult.data["sessionId"] = sessionId;
    visibleResult.data["durationMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
    visibleResult.content = (result.ok ? "" : "! ") + result.message;
    return visibleResult;
}
}
