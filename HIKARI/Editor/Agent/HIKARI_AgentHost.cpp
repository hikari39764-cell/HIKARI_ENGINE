#include "Editor/Agent/HIKARI_AgentHost.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace HIKARI::EDITOR::AGENT {
namespace {

constexpr size_t kMaximumShownParameterBytes = 800u;

nlohmann::json Failure(std::string_view code, std::string message) {
    return {{"ok", false}, {"code", std::string(code)}, {"message", std::move(message)}};
}

nlohmann::json ToJson(const EditorOperationResult& result) {
    return {{"ok", result.ok},     {"code", result.code},       {"message", result.message},
            {"data", result.data}, {"details", result.details}, {"hints", result.hints}};
}

nlohmann::json AccessFailure(std::string_view operation) {
    nlohmann::json result =
        Failure("PROJECT_ACCESS_REQUIRED", "当前项目访问模式不允许调用 " + std::string(operation) + "。");
    result["hints"] = nlohmann::json::array({"由用户在智能体面板切换到“开发项目”模式。"});
    return result;
}

} // namespace

bool AllowsAgentOperation(AgentAccessMode mode, std::string_view operation) noexcept {
    // 項目源码の構築と、新しい模块を読み込む再起動。
    return mode == AgentAccessMode::DevelopProject ||
           (!operation.starts_with("project.build") && operation != "project.reload");
}

AgentHost::AgentHost() = default;

AgentHost::~AgentHost() { SetOpen(false); }

void AgentHost::SetExecutor(Executor executor) {
    std::lock_guard lock(mutex_);
    executor_ = std::move(executor);
}

void AgentHost::SetPermissionResolver(PermissionResolver resolver) {
    std::lock_guard lock(mutex_);
    permissionResolver_ = std::move(resolver);
}

bool AgentHost::IsCanceled(const PendingAction& action) const noexcept {
    return !open_.load() || action.generation != generation_.load();
}

void AgentHost::CancelPending() {
    {
        std::lock_guard lock(mutex_);
        generation_.fetch_add(1);
        turnGrants_.clear();
    }
    changed_.notify_all();
}

void AgentHost::SetOpen(bool open) {
    if (open_.exchange(open) != open && !open)
        CancelPending();
}

void AgentHost::SetAccessMode(AgentAccessMode mode) {
    if (!TEXT::IsNamedEnum(kAgentAccessModeNames, mode) || accessMode_.exchange(mode) == mode)
        return;
    CancelPending();
}

nlohmann::json AgentHost::Execute(std::string_view body) {
    if (!open_.load())
        return Failure("BRIDGE_CLOSED", "本地桥已关闭。");
    nlohmann::json request = nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
    if (!request.is_object() || !request.contains("operation") || !request["operation"].is_string())
        return Failure("INVALID_REQUEST", "请求体必须是 {\"operation\": 操作 ID, \"parameters\": {...}}。");
    auto action = std::make_shared<PendingAction>();
    action->operation = request["operation"].get<std::string>();
    if (const auto parameters = request.find("parameters"); parameters != request.end()) {
        if (!parameters->is_object())
            return Failure("INVALID_REQUEST", "parameters 必须是对象。");
        action->parameters = std::move(*parameters);
    }
    if (!AllowsAgentOperation(accessMode_.load(), action->operation))
        return AccessFailure(action->operation);
    std::unique_lock lock(mutex_);
    action->generation = generation_.load();
    pending_.push_back(action);
    changed_.wait(lock, [this, &action] { return action->result.has_value() || IsCanceled(*action); });
    if (!action->result)
        return Failure("ACTION_CANCELED", "动作已被取消（本地桥已关闭或访问模式已切换）。");
    return std::move(*action->result);
}

nlohmann::json AgentHost::Run(const PendingAction& action) {
    Executor executor;
    {
        std::lock_guard lock(mutex_);
        executor = executor_;
    }
    if (!executor)
        return Failure("EXECUTOR_UNAVAILABLE", "编辑器还没有接上操作执行器。");
    try {
        return ToJson(executor(action.operation, action.parameters));
    } catch (const std::exception& error) {
        return Failure("ACTION_FAILED", error.what());
    } catch (...) {
        return Failure("ACTION_FAILED", "动作执行失败。");
    }
}

void AgentHost::PumpActions() {
    std::vector<std::shared_ptr<PendingAction>> actions;
    PermissionResolver resolver;
    {
        std::lock_guard lock(mutex_);
        // 取り消しで待ち手が消えた承認要求は捨てる。
        std::erase_if(awaitingApproval_, [this](const auto& action) { return IsCanceled(*action); });
        if (pending_.empty())
            return;
        actions.swap(pending_);
        resolver = permissionResolver_;
    }
    for (const std::shared_ptr<PendingAction>& action : actions) {
        nlohmann::json result;
        if (IsCanceled(*action)) {
            result = Failure("ACTION_CANCELED", "排队中的动作已取消。");
        } else if (!AllowsAgentOperation(accessMode_.load(), action->operation)) {
            result = AccessFailure(action->operation);
        } else if (!action->approved && !resolver) {
            result = Failure("PERMISSION_RESOLVER_UNAVAILABLE", "动作未执行：编辑器操作权限解析器未接线。");
        } else {
            std::optional<AgentOperationPolicy> policy;
            if (!action->approved) {
                try {
                    policy = resolver(action->operation);
                } catch (...) {
                    result = Failure("PERMISSION_RESOLUTION_FAILED", "动作未执行：权限解析失败。");
                }
            }
            if (result.is_null() && policy && RequiresUserApproval(policy->permission)) {
                std::lock_guard lock(mutex_);
                if (policy->approveEachTime || !turnGrants_.contains(action->operation)) {
                    // 結果を返さずに保留し、呼び出し元は承認か拒否まで待つ。
                    action->permission = TEXT::EnumToName(kEditorOperationPermissionNames, policy->permission);
                    action->approveEachTime = policy->approveEachTime;
                    action->approvalId = nextApprovalId_++;
                    awaitingApproval_.push_back(action);
                    continue;
                }
            }
            if (result.is_null())
                result = Run(*action);
        }
        std::lock_guard lock(mutex_);
        action->result = std::move(result);
    }
    changed_.notify_all();
}

std::vector<AgentHost::Approval> AgentHost::PendingApprovals() const {
    std::vector<Approval> approvals;
    std::lock_guard lock(mutex_);
    for (const std::shared_ptr<PendingAction>& action : awaitingApproval_) {
        std::string parameters = action->parameters.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (parameters.size() > kMaximumShownParameterBytes)
            parameters = parameters.substr(0u, kMaximumShownParameterBytes) + "...";
        approvals.push_back({action->approvalId, action->operation, action->permission, std::move(parameters),
                             action->approveEachTime});
    }
    return approvals;
}

bool AgentHost::HasPendingApprovals() const {
    std::lock_guard lock(mutex_);
    return !awaitingApproval_.empty();
}

void AgentHost::ResolveApproval(uint64_t id, ApprovalDecision decision) {
    {
        std::lock_guard lock(mutex_);
        const auto found = std::find_if(awaitingApproval_.begin(), awaitingApproval_.end(),
                                        [id](const auto& action) { return action->approvalId == id; });
        if (found == awaitingApproval_.end())
            return;
        const std::shared_ptr<PendingAction> action = *found;
        awaitingApproval_.erase(found);
        if (decision == ApprovalDecision::Deny) {
            action->result =
                Failure("USER_REJECTED", "用户拒绝执行此操作。不要重复提交；需要时说明理由并等待用户指示。");
        } else {
            if (decision == ApprovalDecision::AllowTurn && !action->approveEachTime)
                turnGrants_.insert(action->operation);
            // 次のフレームの PumpActions で実行する。
            action->approved = true;
            pending_.push_back(action);
            return;
        }
    }
    changed_.notify_all();
}

void AgentHost::EndTurn() {
    std::lock_guard lock(mutex_);
    turnGrants_.clear();
}

} // namespace HIKARI::EDITOR::AGENT
