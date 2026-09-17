#include "Editor/Workspaces/Vfx/HIKARI_VfxTimelineEditing.h"
#include "Assets/Vfx/HIKARI_VfxLayerAnimation.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace HIKARI::EDITOR {
namespace {
float Span(const VFX::VfxAssetData &asset, const VFX::VfxEmitterData &layer) {
    return layer.activeDuration > 0 ? layer.activeDuration : asset.durationSeconds - layer.startDelay;
}
bool Selected(std::span<const std::string> ids, const std::string &id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
} // namespace
std::vector<VfxTimelineCurve> CollectVfxTimelineCurves(const VFX::VfxEmitterData &layer,
                                                       bool compositionOnly) {
    std::vector<VfxTimelineCurve> result;
    const auto add = [&](std::string_view module, const char *property, const char *label,
                         const char *channels) {
        if (compositionOnly && layer.kind == VFX::VfxLayerKind::Particles)
            return;
        for (const char *c = channels; *c; ++c)
            result.push_back({{layer.id, std::string(module), property, std::string(1, *c)}, label});
    };
    using Mode = VFX::VfxAnimatedValueMode;
    if (layer.position.value.mode == Mode::Curve)
        add(layer.position.id, "value", "Position", "xyz");
    if (layer.rotation.eulerDegrees.mode == Mode::Curve)
        add(layer.rotation.id, "eulerDegrees", "Rotation (degrees)", "xyz");
    if (layer.scale.value.mode == Mode::Curve)
        add(layer.scale.id, "value", "Scale", "xyz");
    if (layer.color.value.mode == VFX::VfxAnimatedColorMode::Curve)
        add(layer.color.id, "value", "Color", "rgba");
    const auto parameters = VFX::GetVfxAnimatableParameters(layer);
    for (const auto &track : layer.parameterTracks) {
        const auto parameter = std::find_if(parameters.begin(), parameters.end(), [&](const auto &p) {
            return p.targetId == track.targetId && p.propertyId == track.propertyId;
        });
        result.push_back({{layer.id, track.id, "curve", "x"},
                          parameter == parameters.end() ? track.propertyId : parameter->label});
    }
    return result;
}

bool ResolveVfxLayerKeyTime(const VFX::VfxAssetData &asset, const VFX::VfxEmitterData &layer, float seconds,
                            float &normalized, std::string &error) {
    const float span = Span(asset, layer);
    const float local = seconds - layer.startDelay;
    normalized = (layer.animationTiming.offset + local * layer.animationTiming.rate) /
                 layer.animationTiming.Duration(span);
    if (!std::isfinite(seconds) || span <= 0 || local < 0 || local > span || normalized < -0.00001f ||
        normalized > 1.00001f) {
        error = "Key must fit the active layer and original animation range";
        return false;
    }
    normalized = std::clamp(normalized, 0.0f, 1.0f);
    return true;
}

bool MoveVfxTimelineKeys(VFX::VfxAssetData &asset, std::span<const std::string> ids, float deltaSeconds,
                         std::string &error) {
    if (ids.empty() || !std::isfinite(deltaSeconds)) {
        error = "Select keys and a finite time offset";
        return false;
    }
    // 全キーに共通の秒単位の移動量を制限し、選択内の時間差を維持する。
    float minimum = -std::numeric_limits<float>::max(), maximum = std::numeric_limits<float>::max();
    size_t found = 0;
    for (auto &layer : asset.emitters) {
        const float span = layer.animationTiming.Duration(Span(asset, layer)) / layer.animationTiming.rate;
        for (const auto &entry : CollectVfxTimelineCurves(layer, true)) {
            const auto *curve = ResolveVfxCurve(layer, entry.address);
            for (const auto &key : curve->keys)
                if (Selected(ids, key.id)) {
                    ++found;
                    minimum = (std::max)(minimum, -key.time * span);
                    maximum = (std::min)(maximum, (1 - key.time) * span);
                    for (const auto &neighbor : curve->keys)
                        if (!Selected(ids, neighbor.id)) {
                            const float distance = (neighbor.time - key.time) * span;
                            if (distance < 0)
                                minimum = (std::max)(minimum, distance + 0.0001f);
                            else
                                maximum = (std::min)(maximum, distance - 0.0001f);
                        }
                }
        }
    }
    if (found != ids.size() || minimum > maximum) {
        error = "The key selection is stale or has no available interval";
        return false;
    }
    deltaSeconds = std::clamp(deltaSeconds, minimum, maximum);
    for (auto &layer : asset.emitters)
        for (const auto &entry : CollectVfxTimelineCurves(layer, true)) {
            auto *curve = ResolveVfxCurve(layer, entry.address);
            const float span =
                layer.animationTiming.Duration(Span(asset, layer)) / layer.animationTiming.rate;
            if (span <= 0)
                continue;
            for (auto &key : curve->keys)
                if (Selected(ids, key.id))
                    key.time += deltaSeconds / span;
            std::sort(curve->keys.begin(), curve->keys.end(),
                      [](const auto &a, const auto &b) { return a.time < b.time; });
        }
    error.clear();
    return true;
}

std::vector<VfxTimelineClipboardKey> CopyVfxTimelineKeys(const VFX::VfxAssetData &asset,
                                                         std::span<const std::string> ids) {
    std::vector<VfxTimelineClipboardKey> result;
    float origin = std::numeric_limits<float>::max();
    for (const auto &layer : asset.emitters)
        for (const auto &entry : CollectVfxTimelineCurves(layer, true)) {
            const auto *curve = ResolveVfxCurve(layer, entry.address);
            for (const auto &key : curve->keys)
                if (Selected(ids, key.id)) {
                    const float seconds =
                        layer.startDelay + layer.animationTiming.LayerSeconds(key.time, Span(asset, layer));
                    origin = (std::min)(origin, seconds);
                    result.push_back({entry.address, key, seconds});
                }
        }
    for (auto &key : result)
        key.relativeSeconds -= origin;
    return result;
}

bool EditVfxLayerTime(VFX::VfxAssetData &asset, std::string_view layerId, VfxLayerTimeEdit mode,
                      float deltaSeconds, std::string &error) {
    auto it = std::find_if(asset.emitters.begin(), asset.emitters.end(),
                           [&](const auto &layer) { return layer.id == layerId; });
    if (it == asset.emitters.end() || !std::isfinite(deltaSeconds)) {
        error = "Invalid layer time edit";
        return false;
    }
    auto &layer = *it;
    const float oldStart = layer.startDelay, oldSpan = Span(asset, layer);
    constexpr float minimumSpan = 1.0f / 60;
    if (oldSpan <= 0 ||
        (mode != VfxLayerTimeEdit::Move &&
         (oldStart + oldSpan < minimumSpan || asset.durationSeconds - oldStart < minimumSpan))) {
        error = "Layer interval is too short to trim or retime";
        return false;
    }
    if (mode == VfxLayerTimeEdit::Move) {
        layer.startDelay =
            std::clamp(oldStart + deltaSeconds, 0.0f, (std::max)(0.0f, asset.durationSeconds - oldSpan));
        layer.activeDuration = oldSpan;
    } else {
        const float end = oldStart + oldSpan;
        if (mode == VfxLayerTimeEdit::TrimStart) {
            layer.startDelay = std::clamp(oldStart + deltaSeconds, 0.0f, end - minimumSpan);
            layer.activeDuration = end - layer.startDelay;
        } else
            layer.activeDuration =
                std::clamp(oldSpan + deltaSeconds, minimumSpan, asset.durationSeconds - oldStart);
        const float sourceDuration = layer.animationTiming.Duration(oldSpan);
        layer.animationTiming.sourceDuration = sourceDuration;
        if (mode == VfxLayerTimeEdit::Retime) {
            layer.animationTiming.rate *= oldSpan / layer.activeDuration;
            for (auto &burst : layer.spawn.burstTimeline)
                burst.time *= layer.activeDuration / oldSpan;
        } else {
            layer.animationTiming.offset += (layer.startDelay - oldStart) * layer.animationTiming.rate;
            for (auto &burst : layer.spawn.burstTimeline)
                burst.time += oldStart - layer.startDelay;
            std::erase_if(layer.spawn.burstTimeline, [&](const auto &burst) {
                return burst.time < 0 || burst.time > layer.activeDuration;
            });
        }
    }
    error.clear();
    return true;
}
} // namespace HIKARI::EDITOR
