#include "HIKARI_AI_AssistantSession.h"
#include "HIKARI_AI_MessageText.h"
#include <algorithm>
#include <utility>
#include <chrono>
namespace HIKARI::EDITOR {

void AiAssistantSession::RunConversation(const std::string& userText) {
    AI_ASSISTANT::AiChatConfig config = SnapshotConfig();
    config.shouldCancel=[this] { return stopRequested_.load(); };
    std::vector<AI_ASSISTANT::AiChatMessage> conversation;
    {
        std::lock_guard<std::mutex> lock(messageMutex_);
        conversation = messages_;
    }
    (void)userText;
    int consecutiveFailureRounds = 0;
    int protocolRepairRounds = 0;
    const int maxToolRounds = std::clamp(config.maxToolRounds, 1, 120);
    std::string failureSummary;
    const std::string systemPrompt = BuildSystemPrompt();
    const auto executeTask=[this](const std::string& json) {
        auto parameters=nlohmann::json::parse(json);
        parameters["_backgroundTask"]=true;
        return ExecuteRequest(parameters.dump());
    };
    for (int turn = 0; turn < maxToolRounds; ++turn) {
        if (stopRequested_.load()) {
            return;
        }
        SetActivity(turn == 0 ? "planning" : "review");
        std::vector<AI_ASSISTANT::AiChatMessage> request = conversation;
        std::string roundContext = "本次任务第 " +
            std::to_string(turn + 1) + "/" +
            std::to_string(maxToolRounds) + " 轮。";
        if (consecutiveFailureRounds >= 2) {
            roundContext +=
                " 你已连续失败 " +
                std::to_string(consecutiveFailureRounds) +
                " 轮。结合错误检查原因，避免重复无效操作。";
        }
        request.push_back({"user", std::move(roundContext)});
        if (!taskContext_.empty()) request.insert(request.begin(),
            {"user", "任务开始时的编辑器快照（前台状态；目标草稿以 sessionId 的查询结果为准）：\n" + taskContext_});
        request.insert(
            request.begin(),
            AI_ASSISTANT::AiChatMessage{
                "system", systemPrompt });
        std::string reply;
        std::string error;
        AI_ASSISTANT::AiChatUsage usage;
        AI_ASSISTANT::AiChatRequestStats requestStats;
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
                    return !stopRequested_.load();
                },
                reply,
                error, &usage, &requestStats);
        RecordUsage(usage, requestStats);
        {
            std::lock_guard<std::mutex> lock(streamMutex_);
            streamingActive_ = false;
        }
        if (stopRequested_.load()) return;
        if (!streamOk) {
            SetActivity("failed");
            AppendMessage({ "assistant", "（调用失败）" + error });
            return;
        }
        if (!error.empty()) {
            reply += "\n\n" + error;
        }
        const std::vector<std::string> actionJsons =
            AI_ASSISTANT::ExtractAllActionJson(reply);
        if (actionJsons.empty()) {
            if (AI_ASSISTANT::HasToolMarkup(reply)) {
                if (protocolRepairRounds++ == 0) {
                    conversation.push_back({"user", "工具调用格式无效，未执行。请重新输出完整的 action JSON，禁止 DSML/XML 标记。"});
                    continue;
                }
                SetActivity("failed");
                AppendMessage({"assistant", "模型返回的操作格式无法解析，已停止。已执行的修改保留在草稿中。"});
                return;
            }
            SetActivity("completed");
            AppendMessage({ "assistant", AI_ASSISTANT::StripActionJsonForDisplay(reply) });
            {
                std::lock_guard<std::mutex> lock(streamMutex_);
                streamingPartial_.clear();
            }
            return;
        }
        std::vector<std::string> executableActions = actionJsons;
        std::string budgetMessage;
        std::erase_if(executableActions, [&budgetMessage](const std::string& json) {
            if (AI_ASSISTANT::ExtractJsonField(json,"action") != "budget.request") return false;
            budgetMessage = "轮数上限由用户设置，助手不能自行增加。请在剩余预算内完成或如实交接。";
            return true;
        });
        AppendProgress(reply);
        conversation.push_back({ "assistant", AI_ASSISTANT::StripActionJsonForDisplay(reply), {}, {{"kind", "progress"}} });
        failureSummary.clear();
        size_t successCount = 0u;
        size_t failureCount = 0u;
        if (!budgetMessage.empty()) {
            conversation.push_back({"user", budgetMessage});
        }
        for (const std::string& actionJson : executableActions) {
            const auto actionName = AI_ASSISTANT::ExtractJsonField(actionJson,"action");
            const auto sessionId = AI_ASSISTANT::ExtractJsonField(actionJson,"sessionId");
            SetActivity("executing", actionName, sessionId);
            const auto started = std::chrono::steady_clock::now();
            AI_ASSISTANT::AiRequestResult result =
                executeTask(actionJson);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (result.ok && result.data.value("pending", false) && result.data.contains("poll")) {
                if (stopRequested_.load()) return;
                SetActivity(result.data.value("stage",std::string("render")), actionName, sessionId);
                if (std::chrono::steady_clock::now() >= deadline) {
                    result.ok = false;
                    result.code = "PREVIEW_TIMEOUT";
                    result.message = "预览超时，停在" + result.data.value("stage", std::string("render")) + "阶段。尚未取得图片。";
                    if (result.data.contains("renderFailure")) result.message += " " + result.data["renderFailure"].get<std::string>();
                    result.data.erase("poll"); result.data["pending"] = false;
                    cancelOwnerRequested_.store(true);
                    break;
                }
                const std::string poll = result.data["poll"].dump();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                result = executeTask(poll);
            }
            if (stopRequested_.load()) return;
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
            if (!result.ok) visibleResult.data["batchStopped"] = true;
            if (!sessionId.empty()) visibleResult.data["sessionId"] = sessionId;
            visibleResult.data["durationMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
            visibleResult.content = (result.ok ? "" : "! ") + result.message;
            AppendMessage(visibleResult);
            if (!result.ok && (result.code == "PREVIEW_TIMEOUT" || result.code == "CAPTURE_FAILED" ||
                result.code == "PREVIEW_FAILED" || result.code == "IMAGE_READ_FAILED")) {
                SetActivity("failed", actionName, sessionId);
                AppendMessage({"assistant", "预览验证未完成：" + result.message +
                    "\n已停止本次任务，保留草稿。可以打开草稿检查；不会把截图链路故障当作作品错误继续修改。"});
                return;
            }
            conversation.push_back(std::move(visibleResult));
            if (result.ok) {
                ++successCount;
            } else {
                ++failureCount;
                failureSummary +=
                    (failureSummary.empty() ? "" : "\n") +
                    actionName + ": " + result.message;
            }
            if (!result.ok) break;
        }
        if (failureSummary.size() > 2000u) {
            failureSummary =
                AI_ASSISTANT::TruncateUtf8(failureSummary,2000);
        }
        if (successCount == 0u && failureCount > 0u) {
            ++consecutiveFailureRounds;
            if (consecutiveFailureRounds >= 3) {
                SetActivity("failed");
                AppendMessage({"assistant", "连续三轮未能执行成功，已停止并保留草稿。\n" + failureSummary});
                return;
            }
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
        "\n可在设置中调整轮数上限，或继续对话。";
    SetActivity("failed");
    AppendMessage({ "assistant", stopMessage });
    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        streamingPartial_.clear();
    }
}

bool AiAssistantSession::SubmitMessage(std::string text, std::vector<AI_ASSISTANT::AiImageAttachment> images) {
    if (busy_.load() || projectSwitchPending_) {
        return false;
    }
    text = AI_ASSISTANT::Trim(std::move(text));
    if (text.empty() && images.empty()) {
        return false;
    }
    if (conversationId_.empty()) NewConversation();
    RefreshCapabilities();
    AppendMessage({ "user", text, std::move(images) });
    taskContext_ = SnapshotContext();
    if (worker_.joinable()) {
        stopRequested_.store(true);
        actionCv_.notify_all();
        worker_.join();
    }
    stopRequested_.store(false);
    { std::lock_guard<std::mutex> lock(stateMutex_); activity_ = {}; }
    busy_.store(true);
    worker_ = std::thread([this, text = std::move(text)]() {
        try {
            RunConversation(text);
        } catch (...) {
            SetActivity("failed");
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
    return true;
}
}
