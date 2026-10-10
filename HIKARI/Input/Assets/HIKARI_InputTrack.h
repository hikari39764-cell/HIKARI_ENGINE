#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <json.hpp>

// 入力轨道：固定歩長の tick（回放の開始を 0 とする）ごとに、入力動作（InputActionMap の動作名）の値を変える。
// 値は同じ動作の次の変化まで保たれ、length に達したら回放は終わってすべての値が 0 に戻る。
// 生のキーではなく動作名で書くので、割り当てを変えても同じ轨道が使える。智能体の自動テストと、
// 编辑器の「走位录制与重放」が同じ形式を使う。文本资产で、現在の formatVersion だけを読む。
//
// {"assetType": "InputTrack", "formatVersion": 1, "length": 240,
//  "events": [{"tick": 0, "action": "Gameplay.Move", "value": [0, 1]},
//             {"tick": 60, "action": "Gameplay.Jump", "value": 1}, {"tick": 62, "action": "Gameplay.Jump", "value": 0}]}
namespace HIKARI::INPUT {

inline constexpr uint32_t kInputTrackFormatVersion = 1u;
// 1 本の轨道の上限（60 tick/秒で 10 分、変化 4096 回）。
inline constexpr uint64_t kMaximumInputTrackTicks = 36000u;
inline constexpr size_t kMaximumInputTrackEvents = 4096u;

struct InputTrackEvent {
    uint64_t tick = 0;
    std::string actionId{};
    // ボタンと 1 軸は x だけを使う。
    std::array<float, 2> value{};
};

struct InputTrack {
    uint64_t length = 0;
    // tick の昇順。同じ tick の中は書いた順。
    std::vector<InputTrackEvent> events{};
};

bool ReadInputTrack(const nlohmann::json& root, InputTrack& outTrack, std::string& outDiagnostic);
bool LoadInputTrack(const std::filesystem::path& path, InputTrack& outTrack, std::string& outDiagnostic);

} // namespace HIKARI::INPUT
