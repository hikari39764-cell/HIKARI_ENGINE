#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include <json.hpp>

// hikari headless：编辑器を起動していなくても、引擎を無頭で一回動かす（任意で Play、入力轨道の锁步回放、截图）。
// 回帰の確認や、编辑器の外での自動テストに使う。結果は終了コード・截图・ログの警告とエラー。
namespace HIKARI::AGENT_CLI {

struct HeadlessRunOptions {
    std::filesystem::path projectRoot{};
    // 1〜10000（引擎の --headless-render の範囲）。
    uint32_t frames = 600;
    bool play = false;
    std::optional<std::filesystem::path> inputReplay{};
    std::optional<uint32_t> captureFrame{};
    std::filesystem::path captureOutput{};
    bool allowMissingModule = false;
    // 引擎の --runtime-config（省略時は编辑器の既定：编辑器の画面込み）。
    std::filesystem::path runtimeConfig{};
};

// {ok, code, message, data: {exitCode, frames, captures, log, problems}} を返す。
nlohmann::json RunHeadless(const HeadlessRunOptions& options);

} // namespace HIKARI::AGENT_CLI
