#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include "Input/Assets/HIKARI_InputTrack.h"

namespace HIKARI::EDITOR::AGENT {

// play.run の一回分：Play が動くのを待ち、入力轨道を回し、ticks だけ锁步で進め、指定の tick で截图する。
// 場面控制器が毎フレーム進め、play.run.status が結果を返す。
struct AgentPlayRun {
    enum class Phase : uint8_t {
        WaitingForPlay,
        Running,
        Finishing,
        Done,
        Failed,
    };

    uint64_t id = 0;
    Phase phase = Phase::WaitingForPlay;
    uint32_t ticks = 0;
    std::optional<INPUT::InputTrack> input{};
    // 昇順の截图 tick。
    std::vector<uint32_t> captureTicks{};
    size_t nextCapture = 0;
    uint64_t pendingCaptureId = 0;
    uint32_t pendingCaptureTick = 0;
    bool pauseAfter = true;
    // 開始時の固定歩長の完了数。
    uint64_t startTick = 0;
    // 開始時のログの通し番号。結果には以降の警告とエラーを付ける。
    uint64_t logSequenceAtStart = 0;
    // --agent-play-run の時だけ：起動してからこの回を始めるまでの警告とエラー（项目設定や运行时功能の解決、
    // 場面の読み込み）。結果ではこの回の分より前に並べる。
    nlohmann::json startupProblems = nlohmann::json::array();
    nlohmann::json captures = nlohmann::json::array();
    // 位置を記録する物体（trackObjects）。30 tick ごとと終点の世界位置を samples に残す。
    std::vector<uint64_t> trackObjects{};
    uint64_t nextSampleTick = 0;
    nlohmann::json samples = nlohmann::json::array();
    // perf：perfFrom から perfTo までの tick（锁步なので 1 tick が 1 フレーム）を性能捕獲で測る。perfTo が 0 なら測らない。
    // perfReport は捕獲を始めた時に作り、終わると摘要（始められなければ error）が入る。null の間は測っている途中。
    uint32_t perfFrom = 0;
    uint32_t perfTo = 0;
    std::shared_ptr<nlohmann::json> perfReport{};
    std::string error{};
};

} // namespace HIKARI::EDITOR::AGENT
