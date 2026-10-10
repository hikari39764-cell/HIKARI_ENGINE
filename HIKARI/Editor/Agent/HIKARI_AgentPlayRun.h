#pragma once

#include <cstdint>
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
    nlohmann::json captures = nlohmann::json::array();
    // 位置を記録する物体（trackObjects）。30 tick ごとと終点の世界位置を samples に残す。
    std::vector<uint64_t> trackObjects{};
    uint64_t nextSampleTick = 0;
    nlohmann::json samples = nlohmann::json::array();
    std::string error{};
};

} // namespace HIKARI::EDITOR::AGENT
