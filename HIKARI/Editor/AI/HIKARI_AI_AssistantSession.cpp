#include "HIKARI_AI_AssistantSession.h"
#include "HIKARI_AI_MessageText.h"
#include <algorithm>
#include <utility>
namespace HIKARI::EDITOR {
AiAssistantSession::AiAssistantSession(AI_ASSISTANT::AiChatCompletion completion) : completion_(std::move(completion)) {
    mainThreadId_ = std::this_thread::get_id();
    capabilitySnapshot_ = AI_ASSISTANT::BuiltInCapabilities();
    configLoaded_ = AI_ASSISTANT::LoadAiChatConfig(config_);
}

AiAssistantSession::~AiAssistantSession() {
    Stop();
    FlushConversation();
}

std::vector<AI_ASSISTANT::AiChatMessage> AiAssistantSession::SnapshotMessages() const {
    std::lock_guard<std::mutex> lock(messageMutex_);
    return messages_;
}
void AiAssistantSession::SetConfig(AI_ASSISTANT::AiChatConfig config) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    config_ = std::move(config);
}

AiAssistantSession::TaskActivity AiAssistantSession::SnapshotActivity() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return activity_;
}
void AiAssistantSession::SetActivity(std::string phase, std::string action, std::string sessionId) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (activity_.phase != phase || activity_.action != action)
        activity_.since = std::chrono::steady_clock::now();
    activity_.phase = std::move(phase);
    activity_.action = std::move(action);
    if (!sessionId.empty())
        activity_.sessionId = std::move(sessionId);
}

void AiAssistantSession::Stop() {
    Cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AiAssistantSession::Cancel() {
    const bool wasStopped = stopRequested_.exchange(true);
    generation_.fetch_add(1);
    cancelOwnerRequested_.store(true);
    if (IsBusy())
        SetActivity("stopped");
    actionCv_.notify_all();
    if (!wasStopped && IsBusy())
        AppendMessage({"assistant", "已停止。已执行的修改保留在对应文档中，可以继续编辑或撤销。"});
}

void AiAssistantSession::AppendMessage(const AI_ASSISTANT::AiChatMessage& message) {
    auto stored = message;
    if (stored.role == "assistant")
        stored.data["phase"] = SnapshotActivity().phase;
    std::lock_guard<std::mutex> lock(messageMutex_);
    messages_.push_back(std::move(stored));
    conversationDirty_.store(true);
    if (messages_.size() > 120u) {
        // 完了済みのターン単位で整理し、別の依頼の結果を混ぜない。
        const auto nextUser =
            std::find_if(messages_.begin() + 1, messages_.end(), [](const auto& item) { return item.role == "user"; });
        if (nextUser != messages_.end())
            messages_.erase(messages_.begin(), nextUser);
        else {
            size_t removed = 0, failures = 0, entries = 0;
            for (size_t i = 20; i + 60 < messages_.size() && entries < 24;) {
                if (messages_[i].role != "tool") {
                    ++i;
                    continue;
                }
                ++entries;
                removed += messages_[i].data.value("archivedSteps", size_t{1});
                failures +=
                    messages_[i].data.value("archivedFailures", size_t{messages_[i].data.value("ok", true) ? 0u : 1u});
                messages_.erase(messages_.begin() + i);
            }
            if (removed)
                messages_.insert(messages_.begin() + 20,
                                 AI_ASSISTANT::AiChatMessage{"tool",
                                                             "较早的操作明细已压缩，保留步骤计数。",
                                                             {},
                                                             {{"action", "history.compacted"},
                                                              {"ok", failures == 0},
                                                              {"archivedSteps", removed},
                                                              {"archivedFailures", failures}}});
        }
    }
}

void AiAssistantSession::AppendProgress(const std::string& text) {
    const std::string display = AI_ASSISTANT::StripActionJsonForDisplay(text);
    AI_ASSISTANT::AiChatMessage progress{"assistant", display};
    progress.data["kind"] = "progress";
    AppendMessage(progress);
}

void AiAssistantSession::RecordUsage(const AI_ASSISTANT::AiChatUsage& usage,
                                     const AI_ASSISTANT::AiChatRequestStats& stats) {
    if (!stats.sentTextBytes)
        return;
    std::lock_guard<std::mutex> lock(messageMutex_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->role != "user")
            continue;
        auto& total = it->data["usage"];
        if (!total.is_object())
            total = nlohmann::json::object();
        const auto add = [&total](const char* key, int64_t value) {
            total[key] = total.value(key, int64_t{0}) + value;
        };
        add("requests", 1);
        if (usage.inputTokens >= 0) {
            add("inputTokens", usage.inputTokens);
            add("inputReports", 1);
        }
        if (usage.outputTokens >= 0) {
            add("outputTokens", usage.outputTokens);
            add("outputReports", 1);
        }
        if (usage.cachedTokens >= 0) {
            add("cachedTokens", usage.cachedTokens);
            add("cacheReports", 1);
        }
        total["lastSourceTextBytes"] = stats.sourceTextBytes;
        total["lastSentTextBytes"] = stats.sentTextBytes;
        total["lastSentImages"] = stats.sentImages;
        conversationDirty_.store(true);
        break;
    }
}

void AiAssistantSession::SetSceneActionCallback(AiSceneActionCallback callback) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    sceneActionCallback_ = std::move(callback);
}

void AiAssistantSession::SetContextSnapshot(std::string snapshot) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    contextSnapshot_ = std::move(snapshot);
}

void AiAssistantSession::SetContextProvider(std::function<std::string()> provider) {
    std::lock_guard lock(stateMutex_);
    contextProvider_ = std::move(provider);
}

void AiAssistantSession::SetCapabilityProvider(AiCapabilityProvider callback, std::function<uint64_t()> revision) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    capabilityProvider_ = std::move(callback);
    capabilityVersionProvider_ = std::move(revision);
    capabilityRevision_ = ~uint64_t{0};
}

std::vector<AI_ASSISTANT::AiCapability> AiAssistantSession::Capabilities() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return capabilitySnapshot_;
}

void AiAssistantSession::RefreshCapabilities(bool onlyIfChanged) {
    // registry の列挙は所有スレッドだけで行い、通信側へ値の snapshot を渡す。
    AiCapabilityProvider provider;
    std::function<uint64_t()> version;
    uint64_t previous;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        provider = capabilityProvider_;
        version = capabilityVersionProvider_;
        previous = capabilityRevision_;
    }
    if (onlyIfChanged && !version)
        return;
    const auto revision = version ? version() : 0;
    if (onlyIfChanged && previous == revision)
        return;
    auto capabilities = provider ? provider() : AI_ASSISTANT::BuiltInCapabilities();
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        capabilitySnapshot_ = std::move(capabilities);
        capabilityRevision_ = revision;
    }
}

void AiAssistantSession::SetBridgeStatus(const std::string& status) { bridgeStatus_ = status; }

void AiAssistantSession::SetBridgeSessionToken(std::string token) { bridgeSessionToken_ = std::move(token); }

AI_ASSISTANT::AiRequestResult AiAssistantSession::ExecuteRequest(std::string_view json) {
    if (stopRequested_.load()) {
        AI_ASSISTANT::AiRequestResult result{};
        result.code = "ACTION_CANCELED";
        result.message = "任务已停止。";
        return result;
    }

    const std::string action = AI_ASSISTANT::ExtractJsonField(json, "action");
    // Panel-local actions are handled here; every other action is routed to
    // the unified operation registry through the scene callback.
    const bool panelLocalAction = action == "query.capabilities" || action == "vision.analyze_image" ||
                                  action == "context.image" || action == "help";
    if (panelLocalAction) {
        return AI_ASSISTANT::SubmitAiRequest(json, Capabilities(), [this] { return stopRequested_.load(); });
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
    request->generation = generation_.load();
    {
        std::lock_guard<std::mutex> lock(actionMutex_);
        pendingActions_.push_back(request);
    }
    actionCv_.notify_one();
    std::unique_lock<std::mutex> waitLock(actionMutex_);
    actionCv_.wait(waitLock, [this, &request]() {
        return request->result.has_value() || stopRequested_.load() || request->generation != generation_.load();
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

AI_ASSISTANT::AiRequestResult AiAssistantSession::ExecuteRequestNow(std::string_view json) {
    const std::string action = AI_ASSISTANT::ExtractJsonField(json, "action");
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
        "supported: vision.analyze_image",
    };
    return result;
}

void AiAssistantSession::PumpActions() {
    RefreshCapabilities(true);
    UpdateConversationStorage();
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
        AI_ASSISTANT::AiRequestResult result{};
        if (stopRequested_.load() || request->generation != generation_.load()) {
            result.code = "ACTION_CANCELED";
            result.message = "任务已停止，排队中的动作未执行。";
        } else {
            try {
                result = ExecuteRequestNow(request->json);
            } catch (const std::exception& error) {
                result = {false, "ACTION_FAILED", error.what()};
            } catch (...) {
                result = {false, "ACTION_FAILED", "动作执行失败。"};
            }
        }
        {
            std::lock_guard<std::mutex> lock(actionMutex_);
            request->result = std::move(result);
        }
    }
    actionCv_.notify_all();
}

AI_ASSISTANT::AiChatConfig AiAssistantSession::SnapshotConfig() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return config_;
}

std::string AiAssistantSession::SnapshotContext() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return contextSnapshot_;
}

} // namespace HIKARI::EDITOR
