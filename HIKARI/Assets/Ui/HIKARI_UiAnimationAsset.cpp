#include "Assets/Ui/HIKARI_UiAnimationAsset.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_ScalarCurveJson.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace HIKARI {
std::span<const UiMotionPropertyDescriptor> UiMotionProperties() {
    static constexpr std::array properties{
        UiMotionPropertyDescriptor{UiMotionProperty::OffsetX, "offsetX", "uianim.offsetX", 0, -100000, 100000},
        UiMotionPropertyDescriptor{UiMotionProperty::OffsetY, "offsetY", "uianim.offsetY", 0, -100000, 100000},
        UiMotionPropertyDescriptor{UiMotionProperty::ScaleX, "scaleX", "uianim.scaleX", 1, .001f, 100},
        UiMotionPropertyDescriptor{UiMotionProperty::ScaleY, "scaleY", "uianim.scaleY", 1, .001f, 100},
        UiMotionPropertyDescriptor{UiMotionProperty::Rotation, "rotation", "uianim.rotation", 0, -36000, 36000},
        UiMotionPropertyDescriptor{UiMotionProperty::Opacity, "opacity", "uix.opacity", 1, 0, 1},
        UiMotionPropertyDescriptor{UiMotionProperty::Red, "red", "uianim.red", 1, 0, 16},
        UiMotionPropertyDescriptor{UiMotionProperty::Green, "green", "uianim.green", 1, 0, 16},
        UiMotionPropertyDescriptor{UiMotionProperty::Blue, "blue", "uianim.blue", 1, 0, 16},
        UiMotionPropertyDescriptor{UiMotionProperty::Reveal, "reveal", "uix.reveal", 1, 0, 1}};
    return properties;
}
const UiMotionPropertyDescriptor* FindUiMotionProperty(std::string_view id) {
    for (const auto& p : UiMotionProperties())
        if (p.id == id)
            return &p;
    return nullptr;
}
std::span<const char* const> UiMotionTriggerNames() {
    static constexpr const char* names[]{"Manual", "Open", "Close", "Normal", "Hover", "Press", "Disabled"};
    return names;
}
nlohmann::json WriteUiAnimationAsset(const UiAnimationAsset& asset) {
    auto clips = nlohmann::json::array();
    for (const auto& clip : asset.clips) {
        auto tracks = nlohmann::json::array();
        for (const auto& track : clip.tracks) {
            auto keys = nlohmann::json::array();
            for (const auto& key : track.curve.keys)
                keys.push_back(WriteScalarCurveKeyJson(key));
            tracks.push_back({{"id", track.id},
                              {"slot", track.slot},
                              {"property", (static_cast<size_t>(track.property) < UiMotionProperties().size()
                                                ? UiMotionProperties()[static_cast<size_t>(track.property)].id
                                                : "invalid")},
                              {"value", track.curve.value},
                              {"keys", keys}});
        }
        clips.push_back({{"id", clip.id},
                         {"name", clip.name},
                         {"trigger", (static_cast<size_t>(clip.trigger) < UiMotionTriggerNames().size()
                                          ? UiMotionTriggerNames()[static_cast<size_t>(clip.trigger)]
                                          : "invalid")},
                         {"duration", clip.duration},
                         {"loop", clip.loop},
                         {"tracks", tracks}});
    }
    return {{"version", 1}, {"name", asset.name}, {"clips", clips}};
}
bool ReadUiAnimationAsset(const nlohmann::json& data, UiAnimationAsset& out, std::string& error) {
    try {
        auto reject = [&](const char* text) {
            error = text;
            return false;
        };
        if (!data.is_object() || data.value("version", 0) != 1 || !data.contains("clips") || !data["clips"].is_array())
            return reject("Invalid UI animation format/version.");
        UiAnimationAsset asset;
        asset.name = data.value("name", std::string("UI Animation"));
        std::set<uint64_t> clipIds;
        std::set<std::string> names;
        std::set<UiMotionTrigger> triggers;
        for (const auto& c : data["clips"]) {
            UiAnimationClip clip;
            clip.id = c.at("id").get<uint64_t>();
            clip.name = c.at("name").get<std::string>();
            clip.duration = c.value("duration", .3f);
            clip.loop = c.value("loop", false);
            if (!clip.id || !clipIds.insert(clip.id).second || clip.name.empty() || !names.insert(clip.name).second ||
                !std::isfinite(clip.duration) || clip.duration <= 0)
                return reject("Clip IDs/names must be unique and duration must be positive.");
            const std::string trigger = c.value("trigger", std::string("Manual"));
            const auto options = UiMotionTriggerNames();
            auto found = std::find(options.begin(), options.end(), trigger);
            if (found == options.end())
                return reject("Unknown UI animation trigger.");
            clip.trigger = static_cast<UiMotionTrigger>(found - options.begin());
            if (clip.trigger != UiMotionTrigger::Manual && !triggers.insert(clip.trigger).second)
                return reject("Only one clip may own each automatic trigger.");
            if (clip.trigger == UiMotionTrigger::Close && clip.loop)
                return reject("A closing animation cannot loop.");
            std::set<uint64_t> ids;
            std::set<std::pair<std::string, UiMotionProperty>> channels;
            for (const auto& t : c.at("tracks")) {
                UiAnimationTrack track;
                track.id = t.at("id").get<uint64_t>();
                track.slot = t.value("slot", std::string("self"));
                const auto* property = FindUiMotionProperty(t.at("property").get<std::string>());
                if (!property || track.slot.empty() || !track.id || !ids.insert(track.id).second)
                    return reject("Invalid UI animation track or property.");
                track.property = property->property;
                if (!channels.emplace(track.slot, track.property).second)
                    return reject("Two tracks cannot write the same target property in one clip.");
                track.curve.value = t.value("value", property->initial);
                if (!std::isfinite(track.curve.value))
                    return reject("Nonfinite animation value.");
                for (const auto& k : t.at("keys")) {
                    const auto key = ReadScalarCurveKeyJson(k);
                    if (!std::isfinite(key.time) || !std::isfinite(key.value) || !std::isfinite(key.inSlope) ||
                        !std::isfinite(key.outSlope) || key.time < 0 || key.time > clip.duration)
                        return reject("Keys must be finite and inside the clip duration.");
                    track.curve.keys.push_back(key);
                }
                NormalizeScalarCurve(track.curve);
                clip.tracks.push_back(std::move(track));
            }
            asset.clips.push_back(std::move(clip));
        }
        out = std::move(asset);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
bool LoadUiAnimationAsset(const std::filesystem::path& path, UiAnimationAsset& out, std::string& error) {
    nlohmann::json data;
    return SERIALIZATION::JSON::ReadJsonFile(path, data, &error) && ReadUiAnimationAsset(data, out, error);
}
bool SaveUiAnimationAsset(const std::filesystem::path& path, const UiAnimationAsset& asset, std::string& error) {
    UiAnimationAsset checked;
    const auto data = WriteUiAnimationAsset(asset);
    return ReadUiAnimationAsset(data, checked, error) && SERIALIZATION::JSON::WriteJsonFile(path, data, &error);
}
} // namespace HIKARI
