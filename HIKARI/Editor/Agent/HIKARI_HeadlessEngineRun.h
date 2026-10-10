#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <json.hpp>

// 無頭の引擎を一回動かす時の引数と、その回の结果（截图と日志の警告・错误）の拾い方。hikari.exe の headless と
// 编辑器の play.background が共有する。後ろの引擎は本地桥を開かない（人の编辑器の连接文件を上書きしない）。
namespace HIKARI::EDITOR::AGENT {

struct HeadlessEngineRun {
    std::filesystem::path descriptor{};
    // 1〜10000（引擎の --headless-render の範囲）。
    uint32_t frames = 600u;
    bool play = false;
    std::filesystem::path inputReplay{};
    std::filesystem::path runtimeConfig{};
    // 空なら --allow-missing-project-module。
    std::filesystem::path projectModule{};
    // 空なら撮らない。captureCount が 2 以上の時、引擎は「名前_フレーム番号」で書く。
    std::filesystem::path captureOutput{};
    uint32_t captureFrame = 0u;
    uint32_t captureCount = 1u;
    uint32_t captureStride = 1u;
};

// 引擎の实行文件を除いた引数。
std::vector<std::wstring> BuildHeadlessEngineArguments(const HeadlessEngineRun& run);
// 後ろの引擎の運行設定（runtime 節）。人の编辑器の窗口の大きさではなく固定の大きさで描く。light は引擎の既定の画質
// から GI・体積・景深を落とす（人の编辑器と GPU を分け合い、玩法の確認を速く終える）。light でなければ项目の画質。
nlohmann::json HeadlessRuntimeSettings(bool light);
// 実際に書かれた截图 [{frame, path}]。
nlohmann::json CollectHeadlessCaptures(const HeadlessEngineRun& run);

std::filesystem::path FindProjectDescriptor(const std::filesystem::path& projectRoot);
std::set<std::filesystem::path> ListLogFiles(const std::filesystem::path& folder);
// before に無かった日志（その回の引擎が作ったもの）。人の编辑器が同じ目录へ書いていても取り違えない。
std::filesystem::path FindNewLogFile(const std::filesystem::path& folder,
                                     const std::set<std::filesystem::path>& before);
// 日志の警告と错误の行（最大 maximum 行）。
nlohmann::json ReadLogProblems(const std::filesystem::path& log, size_t maximum = 40u);

} // namespace HIKARI::EDITOR::AGENT
