#include "Core/Text/HIKARI_Localization.h"
#include "HIKARI_AI_AssistantSession.h"
#include "HIKARI_AI_MessageText.h"
#include "HIKARI_AI_ToolProtocol.h"
#include "HIKARI_AI_RequestContext.h"
#include <algorithm>
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
    if (!resume)
        task = {true, BuildSystemPrompt(), taskContext_, SnapshotMessages()};
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
        std::string reply, error;
        AiChatUsage usage;
        AiChatRequestStats stats;
        AiToolReply decoded;
        {
            std::lock_guard lock(streamMutex_);
            streamingPartial_.clear();
            streamingActive_ = true;
        }
        const bool ok = completion_(
            config, request,
            [this](const std::string& delta) {
                std::lock_guard lock(streamMutex_);
                streamingPartial_ += delta;
                return !stopRequested_.load();
            },
            reply, error, &usage, &stats, &decoded);
        RecordUsage(usage, stats);
        runTokens += (std::max)(int64_t{0}, usage.inputTokens) + (std::max)(int64_t{0}, usage.outputTokens);
        {
            std::lock_guard lock(streamMutex_);
            streamingActive_ = false;
            streamingPartial_.clear();
        }
        if (stopRequested_.load())
            return;
        if (!ok) {
            SetActivity("failed");
            AppendMessage({"assistant", TEXT::Localize("ai.error.incomplete") + "\n" + error});
            return;
        }
        if (!reply.empty())
            AppendProgress(reply);
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
        bool batchStopped = false, progressed = false;
        std::string failure;
        for (const auto& call : decoded.calls) {
            const auto id = call["id"].get<std::string>();
            const auto name = call["function"]["name"].get<std::string>();
            const auto raw = call["function"]["arguments"].get<std::string>();
            const auto args = Json::parse(raw);
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
                } else
                    fail("Tool call ID reused or interrupted. Inspect state and use a new call ID.");
            } else if (batchStopped) {
                fail("Skipped because an earlier call in this response failed.");
            } else if (name == "task_finish") {
                const auto status = args.value("status", std::string{});
                const auto summary = args.value("summary", std::string{});
                if (decoded.calls.size() != 1 || summary.empty() ||
                    (status != "completed" && status != "needs_input" && status != "blocked")) {
                    fail("task_finish requires a sole call with status and summary.");
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
                        fail("Interrupted; remaining actions were not executed.");
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
    return taskCheckpoint_.active;
}
bool AiAssistantSession::Resume() { return CanResume() && StartConversation({}, {}, true); }
bool AiAssistantSession::SubmitMessage(std::string text, std::vector<AI_ASSISTANT::AiImageAttachment> images) {
    return StartConversation(std::move(text), std::move(images), CanResume());
}
bool AiAssistantSession::StartConversation(std::string text, std::vector<AI_ASSISTANT::AiImageAttachment> images,
                                           bool resume) {
    if (busy_.load() || projectSwitchPending_) {
        return false;
    }
    text = AI_ASSISTANT::Trim(std::move(text));
    if (!resume && text.empty() && images.empty()) {
        return false;
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
            streamingPartial_.clear();
        }
        busy_.store(false);
    });
    return true;
}
} // namespace HIKARI::EDITOR
