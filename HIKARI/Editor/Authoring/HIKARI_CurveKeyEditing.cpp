#include "Editor/Authoring/HIKARI_CurveKeyEditing.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace HIKARI::EDITOR {
namespace {
auto Find(std::span<const CurveEditChannel> channels, const std::string& id) {
    return std::find_if(channels.begin(), channels.end(), [&](const auto& c) { return c.id == id; });
}
float Secant(const ScalarCurveKey& a, const ScalarCurveKey& b) {
    return (b.value - a.value) / (std::max)(.00001f, b.time - a.time);
}
} // namespace
bool CurveKeyEditing::Contains(const std::string& channel, size_t index) const {
    return std::find(selection.begin(), selection.end(), CurveKeyRef{channel, index}) != selection.end();
}
void CurveKeyEditing::Select(CurveKeyRef key, bool toggle) {
    const auto it = std::find(selection.begin(), selection.end(), key);
    if (toggle && it != selection.end())
        selection.erase(it);
    else if (toggle)
        selection.push_back(std::move(key));
    else if (it == selection.end())
        selection = {std::move(key)};
}
void CurveKeyEditing::SelectAll(std::span<const CurveEditChannel> channels) {
    selection.clear();
    for (const auto& c : channels)
        if (c.visible)
            for (size_t i = 0; i < c.curve->keys.size(); ++i)
                selection.push_back({c.id, i});
}
void CurveKeyEditing::Prune(std::span<const CurveEditChannel> channels) {
    std::erase_if(selection, [&](const auto& key) {
        const auto c = Find(channels, key.channel);
        return c == channels.end() || key.index >= c->curve->keys.size();
    });
}
bool CurveKeyEditing::Locked(const CurveEditChannel& channel, size_t index) {
    return std::find(channel.locked.begin(), channel.locked.end(), index) != channel.locked.end();
}
bool CurveKeyEditing::TimeSelected(std::span<const CurveEditChannel> channels, const CurveEditChannel& channel,
                                   size_t index) const {
    for (const auto& key : selection) {
        if (key.index != index)
            continue;
        const auto c = Find(channels, key.channel);
        if (c != channels.end() &&
            (c->id == channel.id || (channel.timeGroup >= 0 && c->timeGroup == channel.timeGroup)))
            return true;
    }
    return false;
}
void CurveKeyEditing::Constrain(CurveEditChannel& c) {
    NormalizeScalarCurve(*c.curve);
    if (!c.monotonic)
        return;
    auto& keys = c.curve->keys;
    // 時間写像の制御点を区間内に保つ。端点を動かさず、逆行と行き過ぎを防ぐ。
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        auto& a = keys[i];
        auto& b = keys[i + 1];
        const float span = b.time - a.time, distance = (std::max)(0.f, b.value - a.value);
        const auto ra = ResolvedScalarKey(keys, i), rb = ResolvedScalarKey(keys, i + 1);
        float out = (std::max)(0.f, ra.outSlope) * span * a.shape.outWeight;
        float in = (std::max)(0.f, rb.inSlope) * span * b.shape.inWeight;
        if (out + in > distance && out + in > 0) {
            const float scale = distance / (out + in);
            out *= scale;
            in *= scale;
        }
        const float sa = out / (std::max)(.000001f, span * a.shape.outWeight);
        const float sb = in / (std::max)(.000001f, span * b.shape.inWeight);
        if (std::abs(sa - ra.outSlope) > .00001f) {
            a.inSlope = ra.inSlope;
            a.shape.tangent = ScalarTangentMode::Free;
        }
        if (std::abs(sb - rb.inSlope) > .00001f) {
            b.outSlope = rb.outSlope;
            b.shape.tangent = ScalarTangentMode::Free;
        }
        a.outSlope = sa;
        b.inSlope = sb;
    }
}
bool CurveKeyEditing::ApplyPreset(std::span<CurveEditChannel> channels, CurveKeyPreset preset) {
    bool changed = false;
    for (auto& c : channels) {
        const auto before = *c.curve;
        auto& keys = c.curve->keys;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (!Contains(c.id, i))
                continue;
            auto key = ResolvedScalarKey(keys, i);
            const auto enableIn = [&] {
                if (!i)
                    return;
                // 隣接キーの補間形式を変える際は、その既存の傾きを保存する。
                auto& previous = keys[i - 1];
                if (previous.interpolation != ScalarCurveInterpolation::Cubic) {
                    previous.outSlope =
                        previous.interpolation == ScalarCurveInterpolation::Linear ? Secant(previous, key) : 0;
                    key.inSlope = previous.outSlope;
                    previous.interpolation = ScalarCurveInterpolation::Cubic;
                }
            };
            const auto enableOut = [&] {
                if (i + 1 < keys.size() && key.interpolation != ScalarCurveInterpolation::Cubic) {
                    key.outSlope = key.interpolation == ScalarCurveInterpolation::Linear ? Secant(key, keys[i + 1]) : 0;
                    keys[i + 1].inSlope = key.outSlope;
                }
                key.interpolation = ScalarCurveInterpolation::Cubic;
            };
            if (preset == CurveKeyPreset::Hold)
                key.interpolation = ScalarCurveInterpolation::Hold;
            else if (preset == CurveKeyPreset::Linear) {
                enableIn();
                enableOut();
                key.inSlope = i ? Secant(keys[i - 1], key) : 0;
                key.outSlope = i + 1 < keys.size() ? Secant(key, keys[i + 1]) : 0;
                if (i) {
                    keys[i - 1] = ResolvedScalarKey(keys, i - 1);
                    keys[i - 1].shape.tangent = ScalarTangentMode::Free;
                    keys[i - 1].outSlope = key.inSlope;
                }
                if (i + 1 < keys.size())
                    keys[i + 1].inSlope = key.outSlope;
                key.shape = {};
                key.interpolation = ScalarCurveInterpolation::Linear;
            } else {
                if (preset != CurveKeyPreset::EaseOut)
                    enableIn();
                if (preset != CurveKeyPreset::EaseIn)
                    enableOut();
                key.shape.tangent = ScalarTangentMode::Free;
                if (preset == CurveKeyPreset::Ease || preset == CurveKeyPreset::EaseIn) {
                    key.inSlope = 0;
                    key.shape.inWeight = 1.f / 3;
                }
                if (preset == CurveKeyPreset::Ease || preset == CurveKeyPreset::EaseOut) {
                    key.outSlope = 0;
                    key.shape.outWeight = 1.f / 3;
                }
                if (preset == CurveKeyPreset::Continuous) {
                    key.inSlope = key.outSlope = (key.inSlope + key.outSlope) * .5f;
                    key.shape.tangent = ScalarTangentMode::Continuous;
                }
                if (preset == CurveKeyPreset::Auto)
                    key.shape.tangent = ScalarTangentMode::Auto;
            }
            keys[i] = key;
        }
        if (*c.curve != before) {
            Constrain(c);
            changed = true;
        }
    }
    return changed;
}
bool CurveKeyEditing::SetHandle(CurveEditChannel& channel, size_t index, bool incoming, float slope, float weight,
                                bool split) {
    auto& keys = channel.curve->keys;
    if (index >= keys.size() || (incoming && !index) || (!incoming && index + 1 == keys.size()))
        return false;
    const auto before = *channel.curve;
    auto key = ResolvedScalarKey(keys, index);
    if (split)
        key.shape.tangent = ScalarTangentMode::Free;
    else if (key.shape.tangent == ScalarTangentMode::Auto)
        key.shape.tangent = ScalarTangentMode::Continuous;
    const bool linked = key.shape.tangent == ScalarTangentMode::Continuous;
    // 線形から手柄へ変える時も隣の既存速度を保ち、連動モードでは両側の区間を同時に開く。
    if ((incoming || linked) && index) {
        keys[index - 1] = ResolvedScalarKey(keys, index - 1);
        keys[index - 1].interpolation = ScalarCurveInterpolation::Cubic;
    }
    if ((!incoming || linked) && index + 1 < keys.size()) {
        keys[index + 1] = ResolvedScalarKey(keys, index + 1);
        key.interpolation = ScalarCurveInterpolation::Cubic;
    }
    (incoming ? key.inSlope : key.outSlope) = slope;
    (incoming ? key.shape.inWeight : key.shape.outWeight) = std::clamp(weight, .001f, .999f);
    if (linked)
        key.inSlope = key.outSlope = slope;
    keys[index] = key;
    Constrain(channel);
    return *channel.curve != before;
}
void CurveKeyEditing::Begin(std::span<const CurveEditChannel> channels) {
    before_.clear();
    for (const auto& c : channels)
        before_.push_back({c.id, *c.curve});
}
bool CurveKeyEditing::Transform(std::span<CurveEditChannel> channels, float deltaTime, float deltaValue, float scale,
                                float pivot) {
    if (!Active())
        return false;
    scale = (std::max)(.001f, scale);
    float lower = -std::numeric_limits<float>::max(), upper = -lower;
    // 全チャンネルで一つの移動量を制限する。隣の未選択キーを越えたり、選択内の間隔を崩したりしない。
    for (const auto& c : channels) {
        const auto snapshot =
            std::find_if(before_.begin(), before_.end(), [&](const auto& s) { return s.channel == c.id; });
        if (snapshot == before_.end())
            continue;
        const auto& keys = snapshot->curve.keys;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (!TimeSelected(channels, c, i) || Locked(c, i))
                continue;
            const float t = pivot + (keys[i].time + c.offset - pivot) * scale - c.offset;
            float lo = 0, hi = c.end;
            if (i && (!TimeSelected(channels, c, i - 1) || Locked(c, i - 1)))
                lo = keys[i - 1].time + .0001f;
            if (i + 1 < keys.size() && (!TimeSelected(channels, c, i + 1) || Locked(c, i + 1)))
                hi = keys[i + 1].time - .0001f;
            lower = (std::max)(lower, lo - t);
            upper = (std::min)(upper, hi - t);
        }
    }
    if (lower > upper)
        return false;
    deltaTime = std::clamp(deltaTime, lower, upper);
    bool changed = false;
    for (auto& c : channels) {
        const auto snapshot =
            std::find_if(before_.begin(), before_.end(), [&](const auto& s) { return s.channel == c.id; });
        if (snapshot == before_.end())
            continue;
        auto next = snapshot->curve;
        for (size_t i = 0; i < next.keys.size(); ++i) {
            if (Locked(c, i))
                continue;
            auto& k = next.keys[i];
            if (TimeSelected(channels, c, i)) {
                k.time = pivot + (k.time + c.offset - pivot) * scale - c.offset + deltaTime;
                if (scale != 1) {
                    k.inSlope /= scale;
                    k.outSlope /= scale;
                }
            }
            if (Contains(c.id, i)) {
                k.value += deltaValue / c.valueScale;
                if (c.monotonic) {
                    const float lo = i ? snapshot->curve.keys[i - 1].value : k.value;
                    const float hi = i + 1 < next.keys.size() ? snapshot->curve.keys[i + 1].value : k.value;
                    k.value = std::clamp(k.value, lo, (std::max)(lo, hi));
                }
            }
        }
        if (*c.curve != next) {
            *c.curve = std::move(next);
            Constrain(c);
            changed = true;
        }
    }
    return changed;
}
bool CurveKeyEditing::Cancel(std::span<CurveEditChannel> channels) {
    bool changed = false;
    for (auto& c : channels)
        for (const auto& before : before_)
            if (before.channel == c.id && *c.curve != before.curve) {
                *c.curve = before.curve;
                changed = true;
            }
    End();
    return changed;
}
void CurveKeyEditing::End() { before_.clear(); }
bool CurveKeyEditing::Delete(std::span<CurveEditChannel> channels) {
    bool changed = false;
    for (auto& c : channels)
        for (size_t i = c.curve->keys.size(); i-- > 0;)
            if (TimeSelected(channels, c, i) && !Locked(c, i)) {
                c.curve->keys.erase(c.curve->keys.begin() + static_cast<ptrdiff_t>(i));
                changed = true;
            }
    if (changed)
        selection.clear();
    return changed;
}
void CurveKeyEditing::Copy(std::span<const CurveEditChannel> channels) {
    clipboard.clear();
    for (const auto& c : channels)
        for (size_t i = 0; i < c.curve->keys.size(); ++i)
            if (TimeSelected(channels, c, i))
                clipboard.push_back({c.id, c.curve->keys[i], c.offset + c.curve->keys[i].time});
    if (!clipboard.empty()) {
        easing = clipboard.front().key;
        const auto c = Find(channels, clipboard.front().channel);
        for (size_t i = 0; i < c->curve->keys.size(); ++i)
            if (c->curve->keys[i].time == easing->time) {
                const auto key = ResolvedScalarKey(c->curve->keys, i);
                const float in = i ? Secant(c->curve->keys[i - 1], key) : 0;
                const float out = i + 1 < c->curve->keys.size() ? Secant(key, c->curve->keys[i + 1]) : 0;
                // 別の距離や単位へ貼っても同じ緩急になるよう、区間の平均速度に対する比率をコピーする。
                easing->inSlope = std::abs(in) > .00001f ? key.inSlope / in : 0;
                easing->outSlope = std::abs(out) > .00001f ? key.outSlope / out : 0;
                break;
            }
    }
}
bool CurveKeyEditing::Paste(std::span<CurveEditChannel> channels, float playhead, const std::string& activeChannel) {
    if (clipboard.empty())
        return false;
    const float first = std::min_element(clipboard.begin(), clipboard.end(), [](const auto& a, const auto& b) {
                            return a.sequenceTime < b.sequenceTime;
                        })->sequenceTime;
    const bool single = std::all_of(clipboard.begin(), clipboard.end(),
                                    [&](const auto& k) { return k.channel == clipboard.front().channel; });
    const auto target = std::find_if(channels.begin(), channels.end(),
                                     [&](const auto& channel) { return channel.id == activeChannel; });
    const int group = single && target != channels.end() ? target->timeGroup : -1;
    std::vector<std::pair<std::string, float>> pasted;
    Begin(channels);
    // 範囲外や保護された端点に当たる貼り付けは全体を戻し、一部だけを失わない。
    for (auto& c : channels) {
        const auto original = *c.curve;
        for (const auto& item : clipboard) {
            const bool direct = (single ? activeChannel : item.channel) == c.id;
            if (!direct && (group < 0 || c.timeGroup != group))
                continue;
            auto key = item.key;
            key.time = playhead + item.sequenceTime - first - c.offset;
            if (key.time < 0 || key.time > c.end) {
                Cancel(channels);
                return false;
            }
            for (const auto i : c.locked)
                if (i < original.keys.size() && std::abs(original.keys[i].time - key.time) < .0001f) {
                    Cancel(channels);
                    return false;
                }
            if (!direct) {
                const float time = key.time;
                key = {time, original.Evaluate(time), original.Velocity(time), original.Velocity(time)};
            }
            SetScalarCurveKey(*c.curve, key.time, key.value);
            for (auto& k : c.curve->keys)
                if (std::abs(k.time - key.time) < .00001f)
                    k = key;
            pasted.emplace_back(c.id, key.time);
        }
        if (*c.curve != original)
            Constrain(c);
    }
    End();
    if (pasted.empty())
        return false;
    selection.clear();
    for (const auto& [id, time] : pasted) {
        const auto c = Find(channels, id);
        for (size_t i = 0; i < c->curve->keys.size(); ++i)
            if (std::abs(c->curve->keys[i].time - time) < .00001f)
                selection.push_back({id, i});
    }
    return true;
}
bool CurveKeyEditing::PasteEasing(std::span<CurveEditChannel> channels) {
    if (!easing)
        return false;
    bool changed = false;
    for (auto& c : channels) {
        bool touched = false;
        for (size_t i = 0; i < c.curve->keys.size(); ++i)
            if (Contains(c.id, i)) {
                const auto key = c.curve->keys[i];
                const float in = easing->inSlope * (i ? Secant(c.curve->keys[i - 1], key) : 0);
                const float out =
                    easing->outSlope * (i + 1 < c.curve->keys.size() ? Secant(key, c.curve->keys[i + 1]) : 0);
                // 入り側の緩急は前区間が所有するため、通常の手柄編集と同じ経路で有効にする。
                touched |= SetHandle(c, i, true, in, easing->shape.inWeight, true);
                touched |= SetHandle(c, i, false, out, easing->shape.outWeight, true);
                auto& target = c.curve->keys[i];
                target.shape = easing->shape;
                target.interpolation = easing->interpolation;
                touched |= target != key;
            }
        if (touched) {
            Constrain(c);
            changed = true;
        }
    }
    return changed;
}
} // namespace HIKARI::EDITOR
