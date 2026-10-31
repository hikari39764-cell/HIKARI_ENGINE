#include "Assets/Models/Loading/Gltf/HIKARI_GltfAnimationReader.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::ASSETS::MODELS::GLTF {
namespace {

// 線形補間の通道から、前後の鍵で補間し直しても許容誤差に収まる鍵を除く（書き出しは毎フレーム鍵を打つので、
// 動かない骨の位置・拡大縮小や、等速で動く区間がそのまま冗長になる）。最初と最後の鍵は残すので長さは変わらない。
// 誤差は再生時と同じ補間（位置・拡大縮小は Lerp、回転は NlerpQ）で測る。全体が一定なら一つの鍵にする。
constexpr float kVec3KeyTolerance = 1.0e-4f;
constexpr float kQuatKeyToleranceRadians = 5.0e-4f;

float Vec3KeyError(const MATH::Vec3& a, const MATH::Vec3& b) {
    return (std::max)({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

float QuatKeyError(const MATH::Quat& a, const MATH::Quat& b) {
    const MATH::Quat na = MATH::NormalizeQ(a), nb = MATH::NormalizeQ(b);
    const float dot = std::abs(na.x * nb.x + na.y * nb.y + na.z * nb.z + na.w * nb.w);
    return 2.0f * std::acos((std::min)(dot, 1.0f));
}

template <class T, class Interpolate, class Error>
void ReduceLinearKeys(std::vector<AnimationKeyframe<T>>& keys, Interpolate interpolate, Error error, float tolerance) {
    if (keys.size() < 2u) {
        return;
    }
    std::vector<AnimationKeyframe<T>> kept;
    kept.reserve(keys.size());
    kept.push_back(keys.front());
    size_t anchor = 0u;
    for (size_t candidate = 2u; candidate < keys.size(); ++candidate) {
        // anchor から candidate までを一本の補間にした時、間の鍵が全部許容誤差に収まるか。
        const float start = keys[anchor].timeSec;
        const float span = keys[candidate].timeSec - start;
        bool fits = span > 0.0f;
        for (size_t middle = anchor + 1u; fits && middle < candidate; ++middle) {
            const float weight = (keys[middle].timeSec - start) / span;
            fits = error(interpolate(keys[anchor].value, keys[candidate].value, weight), keys[middle].value) <= tolerance;
        }
        if (!fits) {
            anchor = candidate - 1u;
            kept.push_back(keys[anchor]);
        }
    }
    kept.push_back(keys.back());
    if (kept.size() == 2u && error(kept.front().value, kept.back().value) <= tolerance) {
        kept.pop_back();
    }
    keys = std::move(kept);
}

void ReduceLinearChannel(NodeAnimationChannel& channel) {
    if (channel.interpolation != AnimationInterpolation::Linear) {
        return;
    }
    ReduceLinearKeys(channel.vec3Keys, [](const MATH::Vec3& a, const MATH::Vec3& b, float t) { return MATH::Lerp(a, b, t); },
                     Vec3KeyError, kVec3KeyTolerance);
    ReduceLinearKeys(channel.quatKeys, [](const MATH::Quat& a, const MATH::Quat& b, float t) { return MATH::NlerpQ(a, b, t); },
                     QuatKeyError, kQuatKeyToleranceRadians);
}

} // namespace

void ReadAnimations(const Json& root, const Json& accessors, const Json& bufferViews, const BufferStorage& buffers,
                    ModelAsset& asset) {
    if (root.contains("animations") && root["animations"].is_array()) {
        for (const auto& animNode : root["animations"]) {
            AnimationClip clip{};
            clip.name = animNode.value("name", "Clip");

            if (!animNode.contains("channels") || !animNode["channels"].is_array() || !animNode.contains("samplers") ||
                !animNode["samplers"].is_array()) {
                continue;
            }

            const auto& channels = animNode["channels"];
            const auto& samplers = animNode["samplers"];
            for (const auto& ch : channels) {
                if (!ch.is_object() || !ch.contains("target") || !ch["target"].is_object()) {
                    continue;
                }
                const int samplerIndex = ch.value("sampler", -1);
                if (samplerIndex < 0 || samplerIndex >= static_cast<int>(samplers.size())) {
                    continue;
                }
                const auto& sampler = samplers[static_cast<size_t>(samplerIndex)];
                const int inputAccessor = sampler.value("input", -1);
                const int outputAccessor = sampler.value("output", -1);
                if (inputAccessor < 0 || outputAccessor < 0) {
                    continue;
                }

                std::vector<float> times;
                if (!ReadScalarAccessor(accessors, bufferViews, buffers, inputAccessor, times)) {
                    continue;
                }

                const std::string interpolation = sampler.value("interpolation", "LINEAR");
                const bool cubicSpline = interpolation == "CUBICSPLINE";

                NodeAnimationChannel channel{};
                channel.targetNode = ch["target"].value("node", -1);
                const std::string path = ch["target"].value("path", "translation");
                if (path == "rotation") {
                    channel.path = AnimationTargetPath::Rotation;
                    std::vector<float> values;
                    if (!ReadFloatAccessor(accessors, bufferViews, buffers, outputAccessor, 4, values, nullptr)) {
                        continue;
                    }
                    const size_t valueStride = cubicSpline ? 12u : 4u;
                    const size_t keyCount = (std::min)(times.size(), values.size() / valueStride);
                    channel.quatKeys.reserve(keyCount);
                    for (size_t i = 0; i < keyCount; ++i) {
                        AnimationKeyframe<MATH::Quat> key{};
                        key.timeSec = times[i];
                        const size_t base = i * valueStride;
                        const size_t valueOffset = cubicSpline ? 4u : 0u;
                        key.value = {values[base + valueOffset + 0u], values[base + valueOffset + 1u],
                                     values[base + valueOffset + 2u], values[base + valueOffset + 3u]};
                        if (cubicSpline) {
                            key.inTangent = {values[base + 0u], values[base + 1u], values[base + 2u],
                                             values[base + 3u]};
                            key.outTangent = {values[base + 8u], values[base + 9u], values[base + 10u],
                                              values[base + 11u]};
                        }
                        channel.quatKeys.push_back(key);
                        if (key.timeSec > clip.durationSec) {
                            clip.durationSec = key.timeSec;
                        }
                    }
                } else {
                    channel.path = (path == "scale") ? AnimationTargetPath::Scale : AnimationTargetPath::Translation;
                    std::vector<float> values;
                    if (!ReadFloatAccessor(accessors, bufferViews, buffers, outputAccessor, 3, values, nullptr)) {
                        continue;
                    }
                    const size_t valueStride = cubicSpline ? 9u : 3u;
                    const size_t keyCount = (std::min)(times.size(), values.size() / valueStride);
                    channel.vec3Keys.reserve(keyCount);
                    for (size_t i = 0; i < keyCount; ++i) {
                        AnimationKeyframe<MATH::Vec3> key{};
                        key.timeSec = times[i];
                        const size_t base = i * valueStride;
                        const size_t valueOffset = cubicSpline ? 3u : 0u;
                        key.value = {values[base + valueOffset + 0u], values[base + valueOffset + 1u],
                                     values[base + valueOffset + 2u]};
                        if (cubicSpline) {
                            key.inTangent = {values[base + 0u], values[base + 1u], values[base + 2u]};
                            key.outTangent = {values[base + 6u], values[base + 7u], values[base + 8u]};
                        }
                        channel.vec3Keys.push_back(key);
                        if (key.timeSec > clip.durationSec) {
                            clip.durationSec = key.timeSec;
                        }
                    }
                }

                if (interpolation == "STEP") {
                    channel.interpolation = AnimationInterpolation::Step;
                } else if (interpolation == "CUBICSPLINE") {
                    channel.interpolation = AnimationInterpolation::CubicSpline;
                } else {
                    channel.interpolation = AnimationInterpolation::Linear;
                }
                ReduceLinearChannel(channel);
                clip.channels.push_back(std::move(channel));
            }
            if (!clip.channels.empty()) {
                asset.animations.push_back(std::move(clip));
            }
        }
    }
}

} // namespace HIKARI::ASSETS::MODELS::GLTF
