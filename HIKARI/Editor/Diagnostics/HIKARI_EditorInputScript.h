#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace HIKARI::EDITOR {

// 診断用：起動参数 --editor-input-script <json> で、指定の帧にマウスとキーの入力を ImGui へ送る。無頭の编辑器で
// クリック・ホバー・右ボタンでの飛行・WASD などの操作を再現して、编辑の操作を截图で確かめる。
// 書式 {"events": [{"frame": 600, "mouse": [x, y]}, {"frame": 600, "mouse": [x, y], "to": [x2, y2], "frames": 60},
//                  {"frame": 610, "button": "left"|"right"|"middle", "down": true},
//                  {"frame": 700, "key": "W", "down": true}, {"frame": 710, "wheel": -3}]}
// 座標は编辑器の窗口のクライアント座標（无头截图の画素と同じ）。键名は ImGui::GetKeyName の名前。
// 帧 N で送った入力は N+1 帧の UI から効く。窗口が隠れている无头の時は実際のマウスに上書きされない。
class EditorInputScript {
public:
    // 起動参数から読む。無いか読めなければ空（読めない時は日志に書く）。
    static std::optional<EditorInputScript> FromCommandLine();

    // 帧 frame の入力を送る。
    void Apply(uint64_t frame);

private:
    struct Event {
        uint64_t frame = 0;
        // マウスの移動。frames > 0 なら mouse から to へ frames 帧かけて動かす。
        std::optional<std::array<float, 2>> mouse{};
        std::array<float, 2> to{};
        uint64_t frames = 0;
        int button = -1;
        // 縦の wheel（正で上）。
        float wheel = 0.0f;
        std::string key{};
        bool down = false;
    };
    std::vector<Event> events_;
};

} // namespace HIKARI::EDITOR
