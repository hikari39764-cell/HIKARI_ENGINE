#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Core/Text/HIKARI_EnumNames.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI::EDITOR::AGENT {

// 項目访问模式。人だけが切り替える。「开发项目」の時だけ、項目源码の構築と CLI への源码書き込み権限を開く。
enum class AgentAccessMode : uint8_t {
    Content,
    DevelopProject,
};

inline constexpr TEXT::EnumName<AgentAccessMode> kAgentAccessModeNames[] = {
    {AgentAccessMode::Content, "content"},
    {AgentAccessMode::DevelopProject, "developProject"},
};

bool AllowsAgentOperation(AgentAccessMode mode, std::string_view operation) noexcept;

// 操作ごとの承認の扱い。
struct AgentOperationPolicy {
    EditorOperationPermission permission = EditorOperationPermission::Unspecified;
    // 本轮允许でまとめず、毎回聞く。
    bool approveEachTime = false;
    // 読むだけの操作。人が編集中でも待たせない。
    bool query = false;
};

// 外部智能体（本地桥）からの操作要求を编辑器の主线程で実行する。要求本文は
// {"operation": 操作 ID, "parameters": {...}}（操作名と引数を分け、引数名 action とぶつからない）。
// 項目ファイルの変更や外部プロセスの起動は、人が承認するまで保留する。
class AgentHost {
public:
    using Executor =
        std::function<EditorOperationResult(const std::string& operation, const nlohmann::json& parameters)>;
    using PermissionResolver = std::function<std::optional<AgentOperationPolicy>(std::string_view operation)>;
    // 人が場面を操作中（手柄や数値のドラッグなど）か。主线程で引く。
    using EditingCheck = std::function<bool()>;

    AgentHost();
    ~AgentHost();
    AgentHost(const AgentHost&) = delete;
    AgentHost& operator=(const AgentHost&) = delete;

    void SetExecutor(Executor executor);
    // 主线程で、待っている操作の前に引く。無ければ承認の要る操作を含め何も実行しない。
    void SetPermissionResolver(PermissionResolver resolver);
    // 人が操作している間は、読むだけでない操作を次のフレームへ回す（人の編集と一つの撤销に混ざらないように）。
    // kMaximumDeferral を過ぎても終わらなければ DOCUMENT_BUSY で返す。
    void SetEditingCheck(EditingCheck check);
    static constexpr std::chrono::seconds kMaximumDeferral{20};

    // 本地桥の接続スレッドから呼ぶ（主线程から呼ぶと PumpActions を待ち続ける）。結果の JSON を返す。
    nlohmann::json Execute(std::string_view body);
    // 主线程で毎フレーム呼ぶ。
    void PumpActions();

    // 閉じると待っている要求と承認を取り消し、新しい要求はすぐ失敗する。
    void SetOpen(bool open);
    bool IsOpen() const noexcept { return open_.load(); }

    AgentAccessMode AccessMode() const noexcept { return accessMode_.load(); }
    // 切り替えると、待っている要求・承認・本轮允许を取り消す。
    void SetAccessMode(AgentAccessMode mode);

    enum class ApprovalDecision : uint8_t {
        AllowOnce,
        // 同じ操作を、この一轮（EndTurn まで）は聞かずに通す。
        AllowTurn,
        Deny,
    };
    struct Approval {
        uint64_t id = 0;
        std::string operation, permission, parameters;
        bool approveEachTime = false;
    };
    std::vector<Approval> PendingApprovals() const;
    bool HasPendingApprovals() const;
    void ResolveApproval(uint64_t id, ApprovalDecision decision);
    // 智能体の一轮が終わった。本轮允许を消す。
    void EndTurn();

private:
    struct PendingAction {
        std::string operation{};
        nlohmann::json parameters = nlohmann::json::object();
        uint64_t generation = 0;
        std::optional<nlohmann::json> result{};
        uint64_t approvalId = 0;
        std::string permission{};
        bool approveEachTime = false;
        bool approved = false;
        bool query = false;
        std::optional<std::chrono::steady_clock::time_point> deferredSince{};
    };

    bool IsCanceled(const PendingAction& action) const noexcept;
    void CancelPending();
    nlohmann::json Run(const PendingAction& action);

    mutable std::mutex mutex_{};
    std::condition_variable changed_{};
    std::vector<std::shared_ptr<PendingAction>> pending_{};
    std::vector<std::shared_ptr<PendingAction>> awaitingApproval_{};
    std::set<std::string, std::less<>> turnGrants_{};
    uint64_t nextApprovalId_ = 1;
    Executor executor_{};
    PermissionResolver permissionResolver_{};
    EditingCheck editingCheck_{};
    std::atomic<bool> open_{false};
    std::atomic<uint64_t> generation_{1};
    std::atomic<AgentAccessMode> accessMode_{AgentAccessMode::Content};
};

} // namespace HIKARI::EDITOR::AGENT
