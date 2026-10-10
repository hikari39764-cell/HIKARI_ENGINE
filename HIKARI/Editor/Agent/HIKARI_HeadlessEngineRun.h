#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <json.hpp>

// 無頭の编辑器で play.run を一回走らせる時の引数と、その回の结果の拾い方。hikari.exe の headless と编辑器の
// play.background が共有する。後ろの编辑器は request.json の play.run を走らせ、同じ目录の result.json に
// play.run.status と同じ結果を書く（截图は游戏画面だけ）。本地桥は開かない（人の编辑器の连接文件を上書きしない）。
namespace HIKARI::EDITOR::AGENT {

// 一回で回せる tick の上限。引擎の --headless-render は 10000 フレームまでなので、起動と読み込みの分を残す。
inline constexpr uint32_t kMaximumHeadlessTicks = 7200u;

struct HeadlessEngineRun {
    std::filesystem::path descriptor{};
    // play.run の引数（ticks、track / path、captureAt、trackObjects）。結果は同じ目录の result.json。
    std::filesystem::path request{};
    uint32_t ticks = 600u;
    std::filesystem::path runtimeConfig{};
    // 空なら --allow-missing-project-module。
    std::filesystem::path projectModule{};
};

// 引擎の实行文件を除いた引数。
std::vector<std::wstring> BuildHeadlessEngineArguments(const HeadlessEngineRun& run);
// 後ろの引擎の運行設定（runtime 節）。人の编辑器の窗口の大きさではなく固定の大きさで描く。light は引擎の既定の画質
// から GI・体積・景深を落とす（人の编辑器と GPU を分け合い、玩法の確認を速く終える）。light でなければ项目の画質。
nlohmann::json HeadlessRuntimeSettings(bool light);
// 後ろの编辑器が書いた結果（{ok, code, message, data}）。まだ無ければ null。
nlohmann::json ReadHeadlessPlayResult(const HeadlessEngineRun& run);
// 待つ上限の秒数（起動・読み込みと、無頭で light はおよそ毎秒 20 tick、项目の画質は 4 tick 以上進む分）。
uint32_t HeadlessRunTimeoutSeconds(uint32_t ticks, bool light);

std::filesystem::path FindProjectDescriptor(const std::filesystem::path& projectRoot);
std::set<std::filesystem::path> ListLogFiles(const std::filesystem::path& folder);
// before に無かった日志（その回の引擎が作ったもの）。人の编辑器が同じ目录へ書いていても取り違えない。
std::filesystem::path FindNewLogFile(const std::filesystem::path& folder,
                                     const std::set<std::filesystem::path>& before);
// 日志の警告と错误の行（最大 maximum 行）。結果を書く前に引擎が落ちた時に使う。
nlohmann::json ReadLogProblems(const std::filesystem::path& log, size_t maximum = 40u);

} // namespace HIKARI::EDITOR::AGENT
