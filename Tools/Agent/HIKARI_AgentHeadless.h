#pragma once

#include <filesystem>

#include <json.hpp>

// hikari headless：编辑器を起動していなくても、保存済みの開始場面で play.run を無頭で一回走らせる
// （play.background と同じ仕組み・同じ結果）。回帰の確認や、编辑器の外での自動テストに使う。
namespace HIKARI::AGENT_CLI {

struct HeadlessRunOptions {
    std::filesystem::path projectRoot{};
    // play.run の引数（ticks、track、captureAt、trackObjects）。
    nlohmann::json run = nlohmann::json::object();
    bool projectQuality = false;
    bool allowMissingModule = false;
};

// play.run.status と同じ {ok, code, message, data: {captures, tracked, problems, ...}} を返す。
nlohmann::json RunHeadless(const HeadlessRunOptions& options);

} // namespace HIKARI::AGENT_CLI
