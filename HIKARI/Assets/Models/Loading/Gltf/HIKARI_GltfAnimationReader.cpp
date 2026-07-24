#include "Assets/Models/Loading/Gltf/HIKARI_GltfAnimationReader.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::ASSETS::MODELS::GLTF {

    void ReadAnimations(
        const Json& root,
        const Json& accessors,
        const Json& bufferViews,
        const BufferStorage& buffers,
        ModelAsset& asset) {

        if (root.contains("animations") && root["animations"].is_array()) {
            for (const auto& animNode : root["animations"]) {
                AnimationClip clip{};
                clip.name = animNode.value("name", "Clip");

                if (!animNode.contains("channels") || !animNode["channels"].is_array() ||
                    !animNode.contains("samplers") || !animNode["samplers"].is_array()) {
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

                    const std::string interpolation = sampler.value(
                        "interpolation",
                        "LINEAR");
                    const bool cubicSpline =
                        interpolation == "CUBICSPLINE";

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
                        const size_t keyCount = (std::min)(
                            times.size(),
                            values.size() / valueStride);
                        channel.quatKeys.reserve(keyCount);
                        for (size_t i = 0; i < keyCount; ++i) {
                            AnimationKeyframe<MATH::Quat> key{};
                            key.timeSec = times[i];
                            const size_t base = i * valueStride;
                            const size_t valueOffset = cubicSpline ? 4u : 0u;
                            key.value = {
                                values[base + valueOffset + 0u],
                                values[base + valueOffset + 1u],
                                values[base + valueOffset + 2u],
                                values[base + valueOffset + 3u]
                            };
                            if (cubicSpline) {
                                key.inTangent = {
                                    values[base + 0u],
                                    values[base + 1u],
                                    values[base + 2u],
                                    values[base + 3u]
                                };
                                key.outTangent = {
                                    values[base + 8u],
                                    values[base + 9u],
                                    values[base + 10u],
                                    values[base + 11u]
                                };
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
                        const size_t keyCount = (std::min)(
                            times.size(),
                            values.size() / valueStride);
                        channel.vec3Keys.reserve(keyCount);
                        for (size_t i = 0; i < keyCount; ++i) {
                            AnimationKeyframe<MATH::Vec3> key{};
                            key.timeSec = times[i];
                            const size_t base = i * valueStride;
                            const size_t valueOffset = cubicSpline ? 3u : 0u;
                            key.value = {
                                values[base + valueOffset + 0u],
                                values[base + valueOffset + 1u],
                                values[base + valueOffset + 2u]
                            };
                            if (cubicSpline) {
                                key.inTangent = {
                                    values[base + 0u],
                                    values[base + 1u],
                                    values[base + 2u]
                                };
                                key.outTangent = {
                                    values[base + 6u],
                                    values[base + 7u],
                                    values[base + 8u]
                                };
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
                    clip.channels.push_back(std::move(channel));
                }
                if (!clip.channels.empty()) {
                    asset.animations.push_back(std::move(clip));
                }
            }
        }
    }

} // namespace HIKARI::ASSETS::MODELS::GLTF
