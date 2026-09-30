#include "Core/Text/HIKARI_Localization.h"
#include "HIKARI_AI_AssistantSession.h"
#include "HIKARI_AI_MessageText.h"
#include "HIKARI_AI_ToolProtocol.h"
#include "HIKARI_AI_RequestContext.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <set>

namespace HIKARI::EDITOR {
namespace {
// 中断した呼び出しを再実行せず、欠けた応答だけを閉じる。
void SealInterruptedCalls(std::vector<AI_ASSISTANT::AiChatMessage>& messages) {
    std::set<std::string> pending;
    for (const auto& message : messages) {
        if (message.data.contains("_toolCalls"))
            for (const auto& call : message.data["_toolCalls"])
                pending.insert(call.at("id"));
        if (message.data.contains("_toolCallId"))
            pending.erase(message.data["_toolCallId"].get<std::string>());
    }
    for (const auto& id : pending)
        messages.push_back(
            {"tool",
             "Interrupted. Some actions may already have completed; inspect the document before retrying. No automatic replay.",
             {},
             {{"_toolCallId", id}, {"ok", false}, {"code", "INTERRUPTED"}}});
}

std::string ActionReviewReason(const AI_ASSISTANT::AiChatMessage& result, const nlohmann::json* action) {
    using Json = nlohmann::json;
    const Json& data = result.data;
    if (!result.images.empty())
        return "a new image needs visual review";
    if (!data.is_object())
        return {};
    if (data.value("verificationPending", false))
        return "visual verification is pending";
    if (data.value("pending", false))
        return "the operation is still pending";
    if (data.contains("stateNeedsRefresh") && data["stateNeedsRefresh"].is_boolean() &&
        data["stateNeedsRefresh"].get<bool>())
        return "the document state needs refresh";
    if (data.contains("adjusted")) {
        const Json& adjusted = data["adjusted"];
        if ((adjusted.is_boolean() && adjusted.get<bool>()) ||
            ((adjusted.is_object() || adjusted.is_array()) && !adjusted.empty()))
            return "the applied values differ from the request";
    }
    if (data.contains("appliedValue") && action != nullptr && action->is_object() && action->contains("value")) {
        // Structured typed values report canonicalization through adjusted/stateNeedsRefresh.
        const Json& applied = data["appliedValue"];
        if (!EquivalentOperationValue((*action)["value"], applied))
            return "the applied value differs from the requested value";
    }
    const auto diagnostics = data.find("diagnostics");
    if (diagnostics != data.end() && diagnostics->is_array()) {
        for (const Json& diagnostic : *diagnostics) {
            if (!diagnostic.is_object())
                continue;
            const std::string severity = diagnostic.value("severity", std::string{});
            if (severity == "warning" || severity == "error")
                return "the operation returned warning or error diagnostics";
        }
    }
    const auto hasDiagnostics = [](const Json& value) {
        return value.is_object() &&
               ((value.contains("warnings") && value["warnings"].is_array() && !value["warnings"].empty()) ||
                (value.contains("errors") && value["errors"].is_array() && !value["errors"].empty()));
    };
    if (hasDiagnostics(data))
        return "the operation returned warnings or errors";
    const auto report = data.find("report");
    if (report != data.end() && report->is_object()) {
        if (report->contains("success") && (*report)["success"].is_boolean() && !(*report)["success"].get<bool>())
            return "the operation report is unsuccessful";
        if (hasDiagnostics(*report))
            return "the operation report contains warnings or errors";
    }
    return {};
}
} // namespace
void AiAssistantSession::RunConversation(bool resume) {
    using namespace AI_ASSISTANT;
    using Json = nlohmann::json;
    auto config = SnapshotConfig();
    config.shouldCancel = [this] { return stopRequested_.load(); };
    config.editorTools = true;
    AiTaskCheckpoint task;
    {
        std::lock_guard lock(messageMutex_);
        task = taskCheckpoint_;
    }
    if (!resume) {
        auto history = SnapshotMessages();
        // 表示専用の思考は、新しいタスクのプロトコル履歴に混ぜない。
        for (auto& message : history)
            message.data.erase("_reasoningContent");
        std::erase_if(history, [](const auto& message) {
            return message.data.value("kind", std::string{}) == "progress" && message.content.empty();
        });
        task = {true, BuildSystemPrompt(), taskContext_, std::move(history)};
    }
    config.requestContext = &task.requestContext;
    auto& conversation = task.messages;
    SealInterruptedCalls(conversation);
    if (resume)
        conversation.push_back(
            {"user",
             "继续原任务。已完成的操作不要重做。用户可能修改过文档；先按需核对相关文档版本和会话。当前编辑器状态：\n" +
                 SnapshotContext()});
    const auto checkpoint = [&] {
        std::lock_guard lock(messageMutex_);
        taskCheckpoint_ = task;
        SealInterruptedCalls(taskCheckpoint_.messages);
        conversationDirty_.store(true);
    };
    struct OnExit {
        std::function<void()> save;
        ~OnExit() { save(); }
    } saveOnExit{checkpoint};
    const int maxRounds = std::clamp(config.maxToolRounds, 1, kMaxAiToolRounds);
    int noToolRounds = 0, noProgressRounds = 0;
    int64_t runTokens = 0;
    AiProgressTracker progress;
    struct CachedCall {
        std::string arguments;
        AiChatMessage result;
    };
    std::map<std::string, CachedCall> completedCalls;
    for (const auto& message : conversation) {
        if (message.role == "tool")
            progress.Observe(message.data);
        if (message.data.contains("_toolCalls"))
            for (const auto& call : message.data["_toolCalls"])
                completedCalls[call.at("id")] = {
                    call["function"]["name"].get<std::string>() + call["function"]["arguments"].get<std::string>(), {}};
        if (message.data.contains("_toolCallId"))
            completedCalls[message.data["_toolCallId"]].result = message;
    }
    for (int round = 0; round < maxRounds; ++round) {
        checkpoint();
        if (stopRequested_.load())
            return;
        if (config.maxTaskTokens > 0 && runTokens >= config.maxTaskTokens)
            break;
        SetActivity(round == 0 ? "planning" : "review");
        auto request = conversation;
        request.insert(request.begin(), {"system", task.prompt});
        if (!task.context.empty())
            request.insert(request.begin() + 1,
                           {"user", "任务开始时的编辑器状态；实际文档以操作结果为准：\n" + task.context});
        config.requestContextPrefixMessages = request.size() - conversation.size();
        std::string reply, error;
        AiChatUsage usage;
        AiChatRequestStats stats;
        AiToolReply decoded;
        {
            std::lock_guard lock(streamMutex_);
            streamingPartial_ = {};
            streamingActive_ = true;
        }
        const bool ok = completion_(
            config, request,
            [this](const AiChatDelta& delta) {
                std::lock_guard lock(streamMutex_);
                streamingPartial_.content += delta.content;
                streamingPartial_.reasoning += delta.reasoning;
                return !stopRequested_.load();
            },
            reply, error, &usage, &stats, &decoded);
        RecordUsage(usage, stats);
        runTokens += (std::max)(int64_t{0}, usage.inputTokens) + (std::max)(int64_t{0}, usage.outputTokens);
        AiChatDelta received;
        {
            std::lock_guard lock(streamMutex_);
            streamingActive_ = false;
            received = std::move(streamingPartial_);
            streamingPartial_ = {};
        }
        const bool interrupted = stopRequested_.load();
        const auto& reasoning = decoded.reasoning.empty() ? received.reasoning : decoded.reasoning;
        if (ok || !received.content.empty() || !reasoning.empty())
            AppendProgress(reply.empty() ? received.content : reply, reasoning, !ok || interrupted);
        if (interrupted)
            return;
        if (!ok) {
            SetActivity("failed");
            AppendMessage({"assistant", TEXT::Localize("ai.error.incomplete") + "\n" + error});
            return;
        }
        conversation.push_back(decoded.Message(reply));
        if (decoded.calls.empty()) {
            if (++noToolRounds >= 4) {
                SetActivity("paused");
                AppendMessage({"assistant", TEXT::Localize("ai.error.noTool")});
                return;
            }
            if (noToolRounds == 1)
                conversation.push_back(
                    {"user",
                     "需要执行时调用 editor_actions；回答完成时调用 task_finish。中间说明可以继续，但承诺下一步不代表任务完成。"});
            continue;
        }
        noToolRounds = 0;
        std::string finishValidationError;
        const auto finishCall = std::find_if(decoded.calls.begin(), decoded.calls.end(), [](const Json& call) {
            return call["function"]["name"] == "task_finish";
        });
        const bool hasFinish = finishCall != decoded.calls.end();
        if (hasFinish && std::count_if(decoded.calls.begin(), decoded.calls.end(), [](const Json& call) {
                             return call["function"]["name"] == "task_finish";
                         }) != 1) {
            finishValidationError = "task_finish may appear only once in a response.";
        } else if (hasFinish) {
            const size_t finishIndex = static_cast<size_t>(std::distance(decoded.calls.begin(), finishCall));
            if (finishIndex + 1 != decoded.calls.size()) {
                finishValidationError = "task_finish must be the final tool call in a response.";
            } else {
                const Json arguments =
                    Json::parse((*finishCall)["function"]["arguments"].get<std::string>(), nullptr, false);
                if (!arguments.is_object() || arguments.size() != 2 || !arguments.contains("status") ||
                    !arguments["status"].is_string() || !arguments.contains("summary") ||
                    !arguments["summary"].is_string()) {
                    finishValidationError = "task_finish requires a valid status and nonempty summary.";
                } else {
                    const std::string status = arguments["status"].get<std::string>();
                    const std::string summary = arguments["summary"].get<std::string>();
                    if (AI_ASSISTANT::Trim(summary).empty() ||
                        (status != "completed" && status != "needs_input" && status != "blocked")) {
                        finishValidationError = "task_finish requires a valid status and nonempty summary.";
                    } else if (status != "completed" && decoded.calls.size() != 1) {
                        finishValidationError =
                            "needs_input and blocked must be reported with a sole task_finish call.";
                    } else if (status == "completed") {
                        for (size_t index = 0; index < finishIndex; ++index) {
                            if (decoded.calls[index]["function"]["name"] != "editor_actions") {
                                finishValidationError =
                                    "A completed task_finish may follow only editor_actions calls in the same response.";
                                break;
                            }
                        }
                    }
                }
            }
        }

        bool batchStopped = !finishValidationError.empty(), completionNeedsReview = false, progressed = false;
        std::string failure = batchStopped ? "task_finish:" + finishValidationError : std::string{};
        std::string reviewReason;
        for (const auto& call : decoded.calls) {
            const auto id = call["id"].get<std::string>();
            const auto name = call["function"]["name"].get<std::string>();
            const auto raw = call["function"]["arguments"].get<std::string>();
            const auto args = Json::parse(raw, nullptr, false);
            AiChatMessage response{"tool", "", {}, {{"_toolCallId", id}}};
            const auto fail = [&](const std::string& message) {
                response.content = message;
                response.data["ok"] = false;
                batchStopped = true;
                if (failure.empty())
                    failure = name + ":" + message;
            };
            const auto cached = completedCalls.find(id);
            if (cached != completedCalls.end()) {
                if (cached->second.arguments == name + raw && !cached->second.result.role.empty()) {
                    response = cached->second.result;
                    if (!response.data.value("ok", false))
                        fail(response.content);
                    else if (hasFinish && name == "editor_actions" && response.data.contains("results") &&
                             response.data["results"].is_array()) {
                        const auto requestedActions = args.is_object() ? args.find("actions") : args.end();
                        for (size_t index = 0;
                             requestedActions != args.end() && requestedActions->is_array() &&
                             index < response.data["results"].size() && index < requestedActions->size();
                             ++index) {
                            const Json& receipt = response.data["results"][index];
                            const std::string reason =
                                ActionReviewReason(AiChatMessage{"tool", {}, {}, receipt}, &(*requestedActions)[index]);
                            if (!reason.empty()) {
                                completionNeedsReview = true;
                                if (reviewReason.empty())
                                    reviewReason = reason;
                            }
                        }
                    }
                } else {
                    fail("Tool call ID reused or interrupted. Inspect state and use a new call ID.");
                    response.data["code"] = "TOOL_CALL_REPLAY";
                }
            } else if (!finishValidationError.empty()) {
                fail("Tool response rejected before execution: " + finishValidationError);
                response.data["code"] = "INVALID_TASK_FINISH";
                response.data["deferred"] = true;
            } else if (name == "task_finish" && (batchStopped || stopRequested_.load())) {
                if (stopRequested_.load()) {
                    batchStopped = true;
                    if (failure.empty())
                        failure = "ACTION_CANCELED";
                }
                response.content =
                    "task_finish deferred because " + failure + ". Inspect the receipts and continue review.";
                response.data["ok"] = false;
                response.data["code"] = "TASK_FINISH_DEFERRED";
                response.data["deferred"] = true;
            } else if (stopRequested_.load()) {
                fail("Action canceled before execution; remaining calls were not executed.");
                response.data["code"] = "ACTION_CANCELED";
            } else if (batchStopped) {
                fail("Skipped because an earlier call in this response failed.");
            } else if (!args.is_object()) {
                fail("Tool arguments must be a JSON object.");
            } else if (name == "task_finish") {
                const auto status = args.value("status", std::string{});
                const auto summary = args.value("summary", std::string{});
                if (status == "completed" && completionNeedsReview) {
                    response.content = "task_finish deferred because " + reviewReason +
                                       ". Inspect the receipts and any returned image, then continue review.";
                    response.data["ok"] = false;
                    response.data["code"] = "TASK_FINISH_DEFERRED";
                    response.data["deferred"] = true;
                } else if (stopRequested_.load()) {
                    batchStopped = true;
                    if (failure.empty())
                        failure = "ACTION_CANCELED";
                    response.content =
                        "task_finish deferred because " + failure + ". Inspect the receipts and continue review.";
                    response.data["ok"] = false;
                    response.data["code"] = "TASK_FINISH_DEFERRED";
                    response.data["deferred"] = true;
                } else {
                    response.data["ok"] = true;
                    conversation.push_back(std::move(response));
                    task.active = status != "completed";
                    SetActivity(status);
                    AppendMessage({"assistant", summary});
                    return;
                }
            } else if (name != "editor_actions" || !args.contains("actions") || !args["actions"].is_array() ||
                       args["actions"].empty()) {
                fail("Use editor_actions with a nonempty actions array, or task_finish.");
            } else {
                response.data["results"] = Json::array();
                for (const auto& action : args["actions"]) {
                    if (stopRequested_.load()) {
                        fail("Action canceled; remaining actions were not executed.");
                        response.data["code"] = "ACTION_CANCELED";
                        break;
                    }
                    if (!action.is_object() || !action.contains("action") || !action["action"].is_string()) {
                        fail("Invalid action object; remaining actions skipped.");
                        break;
                    }
                    auto result = ExecuteConversationAction(action.dump());
                    AppendMessage(result);
                    auto receipt = result.data;
                    receipt["message"] = result.content;
                    const std::string actionReviewReason =
                        hasFinish ? ActionReviewReason(result, &action) : std::string{};
                    if (!actionReviewReason.empty()) {
                        completionNeedsReview = true;
                        if (reviewReason.empty())
                            reviewReason = actionReviewReason;
                    }
                    response.data["results"].push_back(std::move(receipt));
                    response.images.insert(response.images.end(), result.images.begin(), result.images.end());
                    if (!result.data.value("ok", false)) {
                        failure =
                            action.value("action", std::string{}) + ":" + result.data.value("code", std::string{});
                        fail(
                            "An action failed; remaining actions were not executed. Use the receipt to recover or change approach.");
                        break;
                    }
                    progressed = progress.Observe(result.data) || progressed;
                }
                response.data["ok"] = !batchStopped;
            }
            completedCalls.emplace(id, CachedCall{name + raw, response});
            conversation.push_back(std::move(response));
            checkpoint();
        }
        if (stopRequested_.load())
            return;
        noProgressRounds = progressed ? 0 : noProgressRounds + 1;
        if (noProgressRounds == 4) {
            const auto notice = TEXT::Localize("ai.task.noProgress");
            AppendMessage({"assistant", notice});
            conversation.push_back({"user", notice + "\n" + failure});
        }
        if (config.maxNoProgressRounds > 0 && noProgressRounds >= config.maxNoProgressRounds) {
            SetActivity("paused");
            AppendMessage({"assistant", TEXT::Localize("ai.error.stalled")});
            return;
        }
    }
    SetActivity("budget");
    AppendMessage({"assistant", TEXT::Localize("ai.error.budget")});
}

bool AiAssistantSession::CanResume() const {
    if (IsBusy())
        return false;
    std::lock_guard lock(messageMutex_);
    return taskCheckpoint_.active &&
           static_cast<uint8_t>(projectAccessMode_.load()) >= static_cast<uint8_t>(conversationRequiredAccess_);
}
bool AiAssistantSession::Resume() { return CanResume() && StartConversation({}, {}, true); }
bool AiAssistantSession::SubmitMessage(std::string text, std::vector<AI_ASSISTANT::AiImageAttachment> images) {
    return StartConversation(std::move(text), std::move(images), CanResume());
}
bool AiAssistantSession::StartConversation(std::string text, std::vector<AI_ASSISTANT::AiImageAttachment> images,
                                           bool resume) {
    if (busy_.load() || projectSwitchPending_ || pendingProjectAccessMode_) {
        return false;
    }
    text = AI_ASSISTANT::Trim(std::move(text));
    if (!resume && text.empty() && images.empty()) {
        return false;
    }
    const AiProjectAccessMode currentAccess = projectAccessMode_.load();
    {
        std::lock_guard lock(messageMutex_);
        if (static_cast<uint8_t>(currentAccess) < static_cast<uint8_t>(conversationRequiredAccess_)) {
            storageError_ = "此对话需要 " +
                            std::string(TEXT::EnumToName(kAiProjectAccessModeNames, conversationRequiredAccess_)) +
                            " 权限。当前模式不会自动提升；请切换模式或新建对话。";
            return false;
        }
        if (static_cast<uint8_t>(currentAccess) > static_cast<uint8_t>(conversationRequiredAccess_)) {
            conversationRequiredAccess_ = currentAccess;
            conversationDirty_.store(true);
        }
    }
    if (conversationId_.empty())
        NewConversation();
    RefreshCapabilities();
    std::function<std::string()> contextProvider;
    {
        std::lock_guard lock(stateMutex_);
        contextProvider = contextProvider_;
    }
    try {
        if (contextProvider)
            SetContextSnapshot(contextProvider());
    } catch (...) {
        SetActivity("failed");
        AppendMessage({"assistant", TEXT::Localize("ai.error.incomplete") + "\nEditor context snapshot failed."});
        return false;
    }
    if (!text.empty() || !images.empty()) {
        AI_ASSISTANT::AiChatMessage message{"user", text, std::move(images)};
        AppendMessage(message);
        if (resume) {
            std::lock_guard lock(messageMutex_);
            taskCheckpoint_.messages.push_back(std::move(message));
        }
    }
    taskContext_ = SnapshotContext();
    if (worker_.joinable()) {
        stopRequested_.store(true);
        actionCv_.notify_all();
        worker_.join();
    }
    stopRequested_.store(false);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        activity_ = {};
    }
    busy_.store(true);
    worker_ = std::thread([this, resume]() {
        try {
            RunConversation(resume);
        } catch (...) {
            SetActivity("failed");
            try {
                AppendMessage({"assistant", "（内部错误，已停止）"});
            } catch (...) {
            }
        }
        {
            std::lock_guard<std::mutex> lock(streamMutex_);
            streamingActive_ = false;
            streamingPartial_ = {};
        }
        busy_.store(false);
    });
    return true;
}
} // namespace HIKARI::EDITOR
