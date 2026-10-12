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
json Rotation(const MATH::Quat& q) { return json::array({q.x, q.y, q.z, q.w}); }
MATH::Quat Rotation(const json& n) {
    if (!n.is_array() || n.size() != 4)
        throw std::runtime_error("Shot pose rotation must be [x, y, z, w]");
    return {n[0].get<float>(), n[1].get<float>(), n[2].get<float>(), n[3].get<float>()};
}
json Pose(const ShotPose& pose, bool timed) {
    json n = {{"position", Vector(pose.position)}, {"rotation", Rotation(pose.rotation)}, {"fov", pose.fov}};
    if (timed)
        n["time"] = pose.time;
    return n;
}
ShotPose Pose(const json& n, bool timed) {
    if (!n.is_object())
        throw std::runtime_error("Shot pose must be an object");
    ShotPose pose;
    if (timed)
        pose.time = n.at("time").get<float>();
    pose.position = Vector(n.at("position"), pose.position);
    pose.rotation = Rotation(n.at("rotation"));
    pose.fov = n.value("fov", pose.fov);
    return pose;
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
    json keys = json::array();
    for (const auto& key : c.keys)
        keys.push_back(Pose(key, true));
    json rail = {{"points", json::array()}, {"loop", c.rail.loop}};
    for (const auto& point : c.rail.points)
        rail["points"].push_back(Vector(point));
    return {{"owned", c.owned},
            {"name", c.name},
            {"anchor", c.anchor.value},
            {"lookAt", c.lookAt.value},
            {"targetOffset", Vector(c.targetOffset)},
            {"aimWeight", Parameter(c.aimWeight)},
            {"pivotDistance", c.pivotDistance},
            {"smoothPath", c.smoothPath},
            {"near", c.nearClip},
            {"far", c.farClip},
            {"base", Pose(c.base, false)},
            {"keys", std::move(keys)},
            {"rail", std::move(rail)},
            {"railT", Parameter(c.railT)},
            {"motion", WriteCameraMotionEffects(c.motion)}};
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
    c.aimWeight = Parameter(n.value("aimWeight", json{}), 1.0f);
    c.pivotDistance = n.value("pivotDistance", 5.0f);
    c.smoothPath = n.value("smoothPath", true);
    c.nearClip = n.value("near", 0.1f);
    c.farClip = n.value("far", 2000.0f);
    if (const auto base = n.find("base"); base != n.end())
        c.base = Pose(*base, false);
    for (const auto& key : n.value("keys", json::array()))
        c.keys.push_back(Pose(key, true));
    if (const auto rail = n.find("rail"); rail != n.end() && rail->is_object()) {
        c.rail.loop = rail->value("loop", false);
        for (const auto& point : rail->value("points", json::array()))
            c.rail.points.push_back(Vector(point));
    }
    c.railT = Parameter(n.value("railT", json{}), 0.0f);
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
