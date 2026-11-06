#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI {
class World;
namespace RUNTIME {
struct DebugReply {
    bool success = false;
    std::string text;
};

// コマンドの処理。argument は名前より後ろの文字列（前後の空白は除いてある）。
using DebugCommandHandler = std::function<DebugReply(World& world, std::string_view argument)>;

struct DebugCommand {
    std::string name;
    std::string usage;
    // help に出す一文の説明。
    std::string summary;
    DebugCommandHandler handler;
};

// World 所有の開発診断。UI/SDK/AI は同じ実行入口を利用する。
// project code の実行器ではなく、公開イベントが拡張口になる。
class RuntimeDebugService final {
public:
    static constexpr size_t kMaximumTraceLines = 128;
    static constexpr size_t kMaximumReplyBytes = 8192;
    // 組み込みのコマンドはここで登録する。
    RuntimeDebugService();
    RuntimeDebugService(const RuntimeDebugService&) = delete;
    RuntimeDebugService& operator=(const RuntimeDebugService&) = delete;
    // 名前が空か登録済みなら false。登録したコマンドは Reset でも消えない。
    bool RegisterCommand(std::string name, std::string usage, std::string summary, DebugCommandHandler handler);
    const std::vector<DebugCommand>& Commands() const noexcept { return commands_; }
    void SetEnabled(World& world, bool enabled);
    DebugReply Execute(World& world, std::string_view command);
    void Observe(World& world);
    void Reset(World& world);
    // trace が拾ったイベントの行を取り出す。コンソールが毎フレーム呼んで表示する。
    std::deque<std::string> TakeTraceLines();

private:
    DebugReply Trace(World& world, std::string_view argument);
    DebugReply Time(World& world, std::string_view argument);
    void AppendTraceLine(std::string text);
    bool enabled_ = false;
    uint64_t lastEvent_ = 0, timeToken_ = 0;
    uint64_t bindingStoreId_ = 0;
    std::string traceEvent_;
    std::deque<std::string> traceLines_;
    std::vector<DebugCommand> commands_;
};
} // namespace RUNTIME
} // namespace HIKARI
