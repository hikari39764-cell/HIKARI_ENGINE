#include "Editor/Diagnostics/HIKARI_EditorQualitySchedule.h"

#include <algorithm>
#include <cstdlib>

#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFieldPath.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Render3D/Settings/HIKARI_RenderQualityFields.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"

namespace HIKARI::EDITOR {

std::optional<EditorQualitySchedule> EditorQualitySchedule::FromCommandLine() {
    const auto text = ReadRuntimeCommandLinePath(L"--editor-quality-at");
    if (!text)
        return std::nullopt;
    return Parse(TEXT::PathToUtf8(*text));
}

std::optional<EditorQualitySchedule> EditorQualitySchedule::Parse(std::string_view text) {
    EditorQualitySchedule schedule;
    size_t begin = 0;
    while (begin < text.size()) {
        const size_t end = (std::min)(text.find(';', begin), text.size());
        const std::string_view item = text.substr(begin, end - begin);
        begin = end + 1;
        const size_t colon = item.find(':');
        const size_t equals = item.find('=');
        if (colon == std::string_view::npos || equals == std::string_view::npos || equals < colon)
            return std::nullopt;
        Entry entry;
        entry.frame = std::strtoull(std::string(item.substr(0, colon)).c_str(), nullptr, 10);
        entry.key = std::string(item.substr(colon + 1, equals - colon - 1));
        const std::string value(item.substr(equals + 1));
        entry.value = nlohmann::json::parse(value, nullptr, false);
        if (entry.value.is_discarded())
            entry.value = value;
        schedule.entries_.push_back(std::move(entry));
    }
    if (schedule.entries_.empty())
        return std::nullopt;
    return schedule;
}

void EditorQualitySchedule::Apply(uint64_t frame) {
    for (Entry& entry : entries_) {
        if (entry.applied || frame < entry.frame)
            continue;
        entry.applied = true;
        RENDER3D::RenderQualitySettings next = RENDER3D::GetRenderQualitySettings();
        std::string error;
        if (!JSONFIELDS::SetField(next, RENDER3D::kRenderQualitySettingsFields, entry.key, entry.value, error)) {
            HIKARI_LOG_ERROR("[QualitySchedule] frame " + std::to_string(frame) + ": cannot set " + error);
            continue;
        }
        RENDER3D::SetRenderQualitySettings(next);
        HIKARI_LOG_INFO("[QualitySchedule] frame " + std::to_string(frame) + ": " + entry.key + " = " +
                        entry.value.dump());
    }
}

} // namespace HIKARI::EDITOR
