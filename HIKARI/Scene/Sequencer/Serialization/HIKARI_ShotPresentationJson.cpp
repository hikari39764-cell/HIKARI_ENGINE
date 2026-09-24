#include "HIKARI_ShotPresentationJson.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace HIKARI::SEQUENCER {
namespace {
using nlohmann::json;
json Vector(MATH::Vec3 v) { return json::array({v.x, v.y, v.z}); }
MATH::Vec3 Vector(const json& n, MATH::Vec3 fallback = {}) {
    if (!n.is_array() || n.size() != 3)
        return fallback;
    return {n[0].get<float>(), n[1].get<float>(), n[2].get<float>()};
}
json Parameter(const ScalarCurve& p) {
    json n = {{"value", p.value}, {"keys", json::array()}};
    for (const auto& k : p.keys)
        n["keys"].push_back({k.time, k.value, k.inSlope, k.outSlope});
    return n;
}
ScalarCurve Parameter(const json& n, float fallback) {
    ScalarCurve p{fallback};
    if (n.is_number()) {
        p.value = n.get<float>();
        return p;
    }
    if (!n.is_object())
        return p;
    p.value = n.value("value", fallback);
    for (const auto& k : n.value("keys", json::array())) {
        if (!k.is_array() || k.size() != 4)
            throw std::runtime_error("Invalid presentation key");
        p.keys.push_back({k[0].get<float>(), k[1].get<float>(), k[2].get<float>(), k[3].get<float>()});
    }
    return p;
}
} // namespace

nlohmann::json WriteCameraMotionEffects(const std::vector<CameraMotionEffect>& effects) {
    json n = json::array();
    for (const auto& e : effects)
        n.push_back({{"id", e.id},
                     {"kind", static_cast<int>(e.kind)},
                     {"enabled", e.enabled},
                     {"amount", Parameter(e.amount)},
                     {"weight", Parameter(e.weight)},
                     {"frequency", e.frequency},
                     {"seed", e.seed},
                     {"start", e.start},
                     {"duration", e.duration},
                     {"fadeIn", e.fadeIn},
                     {"fadeOut", e.fadeOut},
                     {"maximumSpeed", e.maximumSpeed}});
    return n;
}
std::vector<CameraMotionEffect> ReadCameraMotionEffects(const nlohmann::json& n) {
    if (!n.is_array())
        throw std::runtime_error("Camera effects must be an array");
    std::vector<CameraMotionEffect> effects;
    for (const auto& item : n) {
        CameraMotionEffect e;
        e.id = item.value("id", uint64_t{1});
        const int kind = item.value("kind", 0);
        if (kind < 0 || kind > static_cast<int>(CameraMotionKind::SpeedDolly))
            throw std::runtime_error("Unknown camera motion effect");
        e.kind = static_cast<CameraMotionKind>(kind);
        e.enabled = item.value("enabled", true);
        e.amount = Parameter(item.value("amount", json{}), 1);
        e.weight = Parameter(item.value("weight", json{}), 1);
        e.frequency = item.value("frequency", 3.0f);
        e.seed = item.value("seed", 1u);
        e.start = item.value("start", 0.0f);
        e.duration = item.value("duration", 0.0f);
        e.fadeIn = item.value("fadeIn", 0.0f);
        e.fadeOut = item.value("fadeOut", 0.0f);
        e.maximumSpeed = item.value("maximumSpeed", 8.0f);
        effects.push_back(e);
    }
    NormalizeCameraMotionEffects(effects);
    return effects;
}

nlohmann::json WriteShotCamera(const ShotCamera& c) {
    json n = {{"owned", c.owned},
              {"name", c.name},
              {"anchor", c.anchor.value},
              {"lookAt", c.lookAt.value},
              {"targetOffset", Vector(c.targetOffset)},
              {"near", c.nearClip},
              {"far", c.farClip},
              {"channels", json::object()},
              {"motion", json::array()}};
    for (size_t i = 0; i < c.channels.size(); ++i)
        n["channels"][kShotChannelNames[i]] = Parameter(c.channels[i]);
    n["motion"] = WriteCameraMotionEffects(c.motion);
    return n;
}

ShotCamera ReadShotCamera(const nlohmann::json& n) {
    ShotCamera c;
    if (!n.is_object())
        return c;
    c.owned = n.value("owned", false);
    c.name = n.value("name", std::string{"Shot"});
    c.anchor.value = n.value("anchor", uint64_t{});
    c.lookAt.value = n.value("lookAt", uint64_t{});
    c.targetOffset = Vector(n.value("targetOffset", json{}));
    c.nearClip = n.value("near", 0.1f);
    c.farClip = n.value("far", 2000.0f);
    if (const auto channels = n.find("channels"); channels != n.end() && channels->is_object()) {
        for (size_t i = 0; i < c.channels.size(); ++i)
            c.channels[i] = Parameter(channels->value(kShotChannelNames[i], json{}), c.channels[i].value);
    } else {
        // 旧ポーズを読み込み時だけ変換。区間進行度の接線を値/秒へ変換し、補間を維持する。
        struct LegacyPose {
            ShotPose pose;
            float incoming = 0, outgoing = 0;
        };
        std::vector<LegacyPose> poses;
        for (const auto& item : n.value("poses", json::array())) {
            LegacyPose legacy;
            auto& p = legacy.pose;
            p.time = item.value("time", 0.0f);
            p.eye = Vector(item.value("eye", json{}), p.eye);
            p.target = Vector(item.value("target", json{}), p.target);
            p.fov = item.value("fov", 55.0f);
            p.roll = item.value("roll", 0.0f);
            legacy.outgoing = item.value("outgoing", 0.0f);
            legacy.incoming = item.value("incoming", 0.0f);
            if (std::isfinite(p.time)) {
                p.time = (std::max)(0.0f, p.time);
                poses.push_back(legacy);
            }
        }
        std::stable_sort(poses.begin(), poses.end(),
                         [](const auto& a, const auto& b) { return a.pose.time < b.pose.time; });
        for (const auto& legacy : poses)
            SetShotPose(c, legacy.pose, poses.size() > 1);
        for (auto& channel : c.channels) {
            for (size_t i = 0; i < channel.keys.size(); ++i) {
                auto& key = channel.keys[i];
                const auto legacy = std::find_if(poses.rbegin(), poses.rend(), [&](const auto& p) {
                    return std::abs(p.pose.time - key.time) < 0.00001f;
                });
                if (legacy == poses.rend())
                    continue;
                if (i)
                    key.inSlope = legacy->incoming * (key.value - channel.keys[i - 1].value) /
                                  (key.time - channel.keys[i - 1].time);
                if (i + 1 < channel.keys.size())
                    key.outSlope = legacy->outgoing * (channel.keys[i + 1].value - key.value) /
                                   (channel.keys[i + 1].time - key.time);
            }
        }
    }
    c.motion = ReadCameraMotionEffects(n.value("motion", json::array()));
    NormalizeShotCamera(c);
    return c;
}

nlohmann::json WriteScreenEffects(const std::vector<SequenceScreenEffect>& effects) {
    json n = json::array();
    for (const auto& e : effects)
        n.push_back({{"id", e.id},
                     {"name", e.name},
                     {"kind", static_cast<int>(e.kind)},
                     {"enabled", e.enabled},
                     {"start", e.start},
                     {"duration", e.duration},
                     {"fadeIn", e.fadeIn},
                     {"fadeOut", e.fadeOut},
                     {"amount", Parameter(e.amount)},
                     {"weight", Parameter(e.weight)},
                     {"color", Vector(e.color)}});
    return n;
}

std::vector<SequenceScreenEffect> ReadScreenEffects(const nlohmann::json& n) {
    std::vector<SequenceScreenEffect> effects;
    if (!n.is_array())
        return effects;
    for (const auto& item : n) {
        SequenceScreenEffect e;
        e.id = item.value("id", uint64_t{1});
        e.name = item.value("name", std::string{});
        const int kind = item.value("kind", 0);
        if (kind < 0 || kind > static_cast<int>(ScreenEffectKind::Chromatic))
            throw std::runtime_error("Unknown screen effect");
        e.kind = static_cast<ScreenEffectKind>(kind);
        e.enabled = item.value("enabled", true);
        e.start = item.value("start", 0.0f);
        e.duration = item.value("duration", 3.0f);
        e.fadeIn = item.value("fadeIn", 0.0f);
        e.fadeOut = item.value("fadeOut", 0.0f);
        e.amount = Parameter(item.value("amount", json{}), 0.5f);
        e.weight = Parameter(item.value("weight", json{}), 1);
        e.color = Vector(item.value("color", json{}), {1, 1, 1});
        effects.push_back(e);
    }
    NormalizeScreenEffects(effects);
    return effects;
}
} // namespace HIKARI::SEQUENCER
