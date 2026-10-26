#include "HIKARI_ObjectTrackJson.h"

#include <stdexcept>

namespace HIKARI::SEQUENCER {
namespace {
MATH::Vec3 ReadVec3(const nlohmann::json& node, MATH::Vec3 fallback) {
    if (!node.is_array() || node.size() != 3u)
        return fallback;
    return {node[0].get<float>(), node[1].get<float>(), node[2].get<float>()};
}
MATH::Quat ReadQuat(const nlohmann::json& node) {
    if (!node.is_array() || node.size() != 4u)
        return MATH::Quat::Identity();
    return {node[0].get<float>(), node[1].get<float>(), node[2].get<float>(), node[3].get<float>()};
}
} // namespace

void ReadObjectTrackJson(const nlohmann::json& sequence, std::vector<ObjectTrack>& out) {
    std::vector<ObjectTrack> candidate;
    if (const auto tracks = sequence.find("tracks"); tracks != sequence.end() && tracks->is_array()) {
        for (const auto& node : *tracks) {
            if (!node.is_object() || node.value("type", std::string{}) != "object")
                continue;
            ObjectTrack track;
            track.id = node.at("id").get<uint64_t>();
            track.name = node.value("name", std::string("Object"));
            track.enabled = node.value("enabled", true);
            track.target.value = node.value("target", uint64_t{});
            track.restoreOnStop = node.value("restoreOnStop", true);
            track.animateLight = node.value("animateLight", false);
            const auto& keys = node.at("keys");
            if (!keys.is_array() || keys.size() > kMaximumObjectTrackKeys)
                throw std::runtime_error("Invalid object track key list");
            for (const auto& keyNode : keys) {
                ObjectTrackKey key;
                key.time = keyNode.at("time").get<float>();
                key.position = ReadVec3(keyNode.value("position", nlohmann::json{}), {});
                key.rotation = ReadQuat(keyNode.value("rotation", nlohmann::json{}));
                key.scale = ReadVec3(keyNode.value("scale", nlohmann::json{}), {1.0f, 1.0f, 1.0f});
                key.light = keyNode.value("light", 1.0f);
                if (!ParseObjectKeyInterpolation(keyNode.value("interpolation", std::string("smooth")), key.interpolation))
                    throw std::runtime_error("Unknown object key interpolation");
                track.keys.push_back(key);
            }
            candidate.push_back(std::move(track));
        }
    }
    NormalizeObjectTracks(candidate);
    std::string error;
    if (!ValidateObjectTracks(candidate, error))
        throw std::runtime_error(error);
    out = std::move(candidate);
}

void WriteObjectTrackJson(const std::vector<ObjectTrack>& tracks, nlohmann::json& outTracks) {
    for (const auto& track : tracks) {
        nlohmann::json node{{"type", "object"},
                            {"id", track.id},
                            {"name", track.name},
                            {"enabled", track.enabled},
                            {"target", track.target.value},
                            {"restoreOnStop", track.restoreOnStop},
                            {"animateLight", track.animateLight},
                            {"keys", nlohmann::json::array()}};
        for (const auto& key : track.keys) {
            nlohmann::json keyNode{{"time", key.time},
                                   {"position", nlohmann::json::array({key.position.x, key.position.y, key.position.z})},
                                   {"rotation", nlohmann::json::array({key.rotation.x, key.rotation.y, key.rotation.z,
                                                                       key.rotation.w})},
                                   {"scale", nlohmann::json::array({key.scale.x, key.scale.y, key.scale.z})},
                                   {"interpolation", ToString(key.interpolation)}};
            // 灯の倍率は使う時（既定の 1 から変えた時）だけ書く。
            if (key.light != 1.0f)
                keyNode["light"] = key.light;
            node["keys"].push_back(std::move(keyNode));
        }
        outTracks.push_back(std::move(node));
    }
}
} // namespace HIKARI::SEQUENCER
