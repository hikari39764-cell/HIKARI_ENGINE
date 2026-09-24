#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace HIKARI {
class World;
namespace RUNTIME {
struct DebugReply {
    bool success = false;
    std::string text;
};

// World 所有の開発診断。UI/SDK/AI は同じ実行入口を利用する。
// project code の実行器ではなく、公開イベントが拡張口になる。
class RuntimeDebugService final {
public:
    static constexpr size_t kMaximumHistory = 128;
    static constexpr size_t kMaximumReplyBytes = 8192;
    void SetEnabled(World& world, bool enabled);
    bool IsEnabled() const noexcept { return enabled_; }
    DebugReply Execute(World& world, std::string_view command);
    void Observe(World& world);
    void Reset(World& world);
    const std::deque<std::string>& History() const noexcept { return history_; }

private:
    void Append(std::string text);
    bool enabled_ = false;
    uint64_t lastEvent_ = 0, timeToken_ = 0;
    uint64_t bindingStoreId_ = 0;
    std::string traceEvent_;
    std::deque<std::string> history_;
};
} // namespace RUNTIME
} // namespace HIKARI
