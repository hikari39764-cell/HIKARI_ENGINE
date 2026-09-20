#include "HIKARI_ShotPresentationJson.h"
#include <stdexcept>

namespace HIKARI::SEQUENCER {
namespace {
using nlohmann::json;
json Vector(MATH::Vec3 v) { return json::array({v.x, v.y, v.z}); }
MATH::Vec3 Vector(const json &n, MATH::Vec3 fallback = {}) {
  if (!n.is_array() || n.size() != 3)
    return fallback;
  return {n[0].get<float>(), n[1].get<float>(), n[2].get<float>()};
}
json Parameter(const PresentationParameter &p) {
  json n = {{"value", p.value}, {"keys", json::array()}};
  for (const auto &k : p.keys)
    n["keys"].push_back({k.time, k.value, k.inSlope, k.outSlope});
  return n;
}
PresentationParameter Parameter(const json &n, float fallback) {
  PresentationParameter p{fallback};
  if (n.is_number()) {
    p.value = n.get<float>();
    return p;
  }
  if (!n.is_object())
    return p;
  p.value = n.value("value", fallback);
  for (const auto &k : n.value("keys", json::array())) {
    if (!k.is_array() || k.size() != 4)
      throw std::runtime_error("Invalid presentation key");
    p.keys.push_back({k[0].get<float>(), k[1].get<float>(), k[2].get<float>(),
                      k[3].get<float>()});
  }
  return p;
}
} // namespace

nlohmann::json WriteShotCamera(const ShotCamera &c) {
  json n = {{"owned", c.owned},
            {"name", c.name},
            {"anchor", c.anchor.value},
            {"lookAt", c.lookAt.value},
            {"targetOffset", Vector(c.targetOffset)},
            {"near", c.nearClip},
            {"far", c.farClip},
            {"poses", json::array()},
            {"motion", json::array()}};
  for (const auto &p : c.poses)
    n["poses"].push_back({{"time", p.time},
                          {"eye", Vector(p.eye)},
                          {"target", Vector(p.target)},
                          {"fov", p.fov},
                          {"roll", p.roll},
                          {"outgoing", p.outgoing},
                          {"incoming", p.incoming}});
  for (const auto &e : c.motion)
    n["motion"].push_back({{"id", e.id},
                           {"kind", static_cast<int>(e.kind)},
                           {"enabled", e.enabled},
                           {"amount", Parameter(e.amount)},
                           {"weight", Parameter(e.weight)},
                           {"frequency", e.frequency},
                           {"seed", e.seed}});
  return n;
}

ShotCamera ReadShotCamera(const nlohmann::json &n) {
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
  c.poses.clear();
  for (const auto &item : n.value("poses", json::array())) {
    ShotPose p;
    p.time = item.value("time", 0.0f);
    p.eye = Vector(item.value("eye", json{}), p.eye);
    p.target = Vector(item.value("target", json{}), p.target);
    p.fov = item.value("fov", 55.0f);
    p.roll = item.value("roll", 0.0f);
    p.outgoing = item.value("outgoing", 0.0f);
    p.incoming = item.value("incoming", 0.0f);
    c.poses.push_back(p);
  }
  for (const auto &item : n.value("motion", json::array())) {
    CameraMotionEffect e;
    e.id = item.value("id", uint64_t{1});
    const int kind = item.value("kind", 0);
    if (kind < 0 || kind > static_cast<int>(CameraMotionKind::Handheld))
      throw std::runtime_error("Unknown camera motion effect");
    e.kind = static_cast<CameraMotionKind>(kind);
    e.enabled = item.value("enabled", true);
    e.amount = Parameter(item.value("amount", json{}), 1);
    e.weight = Parameter(item.value("weight", json{}), 1);
    e.frequency = item.value("frequency", 3.0f);
    e.seed = item.value("seed", 1u);
    c.motion.push_back(e);
  }
  NormalizeShotCamera(c);
  return c;
}

nlohmann::json
WriteScreenEffects(const std::vector<SequenceScreenEffect> &effects) {
  json n = json::array();
  for (const auto &e : effects)
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

std::vector<SequenceScreenEffect> ReadScreenEffects(const nlohmann::json &n) {
  std::vector<SequenceScreenEffect> effects;
  if (!n.is_array())
    return effects;
  for (const auto &item : n) {
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
