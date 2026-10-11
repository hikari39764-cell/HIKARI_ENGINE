#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

namespace HIKARI::EDITOR {

// 診断用：起動参数 --editor-quality-at "帧:键=值;帧:键=值;..." で、指定の帧に画質設定を切り替える（画質パネルと同じく
// SetRenderQualitySettings を呼ぶ）。键は画質設定の JSON 名で入れ子はドット区切り、值は JSON（解けなければ文字列）。
// 例 "900:dynamicGiQuality=High;1200:directionalShadow.rayTraced=false"。GI の档位切り替えなどを無頭で再現する。
class EditorQualitySchedule {
public:
    // 起動参数から読む。無ければ空。
    static std::optional<EditorQualitySchedule> FromCommandLine();

    // 帧 frame までに来た切り替えをまだなら行う。
    void Apply(uint64_t frame);

private:
    static std::optional<EditorQualitySchedule> Parse(std::string_view text);

    struct Entry {
        uint64_t frame = 0;
        std::string key{};
        nlohmann::json value{};
        bool applied = false;
    };
    std::vector<Entry> entries_;
};

} // namespace HIKARI::EDITOR
