#pragma once
#include "Core/Animation/HIKARI_ScalarCurve.h"
#include <string>
#include <vector>
#include <span>
#include <optional>

namespace HIKARI::EDITOR {
// チャンネル ID とキーの順序で選択する。移動中は並びを保ち、構造編集では選択を再対応する。
struct CurveKeyRef {
    std::string channel;
    size_t index = 0;
    bool operator==(const CurveKeyRef&) const = default;
};
struct CurveEditChannel {
    std::string id, label;
    ScalarCurve* curve = nullptr;
    float offset = 0, end = 1;
    int timeGroup = -1; // XYZ/FOV の同じ姿勢キーは一度だけ時間を編集する。
    std::vector<size_t> locked;
    bool monotonic = false;
    bool visible = true;
    float valueScale = 1, valueOffset = 0;
    std::string unit;
};
enum class CurveKeyPreset { Linear, Hold, Ease, EaseIn, EaseOut, Bezier, Continuous, Auto };
inline constexpr TEXT::EnumName<CurveKeyPreset> kCurveKeyPresetNames[] = {
    {CurveKeyPreset::Linear, "线性"},           {CurveKeyPreset::Hold, "保持"},
    {CurveKeyPreset::Ease, "缓动 Easy Ease"},   {CurveKeyPreset::EaseIn, "缓入 Ease In"},
    {CurveKeyPreset::EaseOut, "缓出 Ease Out"}, {CurveKeyPreset::Bezier, "贝塞尔"},
    {CurveKeyPreset::Continuous, "连续贝塞尔"}, {CurveKeyPreset::Auto, "自动贝塞尔"}};
struct CurveKeyClipboard {
    std::string channel;
    ScalarCurveKey key;
    float sequenceTime = 0;
};
class CurveKeyEditing {
public:
    std::vector<CurveKeyRef> selection;
    std::vector<CurveKeyClipboard> clipboard;
    std::optional<ScalarCurveKey> easing;
    bool Contains(const std::string& channel, size_t index) const;
    void Select(CurveKeyRef key, bool toggle = false);
    void SelectAll(std::span<const CurveEditChannel> channels);
    void Prune(std::span<const CurveEditChannel> channels);
    bool ApplyPreset(std::span<CurveEditChannel> channels, CurveKeyPreset preset);
    bool Delete(std::span<CurveEditChannel> channels);
    void Copy(std::span<const CurveEditChannel> channels);
    bool Paste(std::span<CurveEditChannel> channels, float playhead, const std::string& activeChannel);
    bool PasteEasing(std::span<CurveEditChannel> channels);
    // 操作開始時の原本から毎フレーム計算するため、吸着や往復ドラッグで誤差を積まない。
    void Begin(std::span<const CurveEditChannel> channels);
    bool Transform(std::span<CurveEditChannel> channels, float deltaTime, float deltaValue = 0, float scale = 1,
                   float pivot = 0);
    bool Cancel(std::span<CurveEditChannel> channels);
    void End();
    bool Active() const { return !before_.empty(); }
    static bool Locked(const CurveEditChannel& channel, size_t index);
    static void Constrain(CurveEditChannel& channel);
    static bool SetHandle(CurveEditChannel& channel, size_t index, bool incoming, float slope, float weight,
                          bool split = false);

private:
    struct Snapshot {
        std::string channel;
        ScalarCurve curve;
    };
    std::vector<Snapshot> before_;
    bool TimeSelected(std::span<const CurveEditChannel> channels, const CurveEditChannel& channel, size_t index) const;
};
} // namespace HIKARI::EDITOR
