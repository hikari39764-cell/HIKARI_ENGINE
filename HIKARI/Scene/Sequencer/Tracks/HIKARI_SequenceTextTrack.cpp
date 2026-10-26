#include "HIKARI_SequenceTextTrack.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/Sequencer/HIKARI_SequenceTrack.h"

namespace HIKARI::SEQUENCER {
namespace {
constexpr TEXT::EnumName<SequenceTextLayout> kLayoutNames[]{
    {SequenceTextLayout::Horizontal, "horizontal"},
    {SequenceTextLayout::Vertical, "vertical"},
};
constexpr TEXT::EnumName<SequenceTextAlign> kAlignNames[]{
    {SequenceTextAlign::Start, "start"},
    {SequenceTextAlign::Center, "center"},
    {SequenceTextAlign::End, "end"},
};
constexpr TEXT::EnumName<SequenceTextReveal> kRevealNames[]{
    {SequenceTextReveal::None, "none"},
    {SequenceTextReveal::Typewriter, "typewriter"},
};

bool InRange(float value, float low, float high) noexcept {
    return std::isfinite(value) && value >= low && value <= high;
}
} // namespace

bool ValidateSequenceTextTrack(const SequenceTextTrack& track, std::string& error) {
    error.clear();
    if (track.clips.size() > kMaximumSequenceTexts) {
        error = "Text track exceeds 256 clips";
        return false;
    }
    std::unordered_set<uint64_t> ids;
    for (const auto& clip : track.clips) {
        const auto invalid = [&](const char* message) {
            error = "Text " + std::to_string(clip.id) + ": " + message;
            return false;
        };
        if (clip.id == 0u || !ids.insert(clip.id).second)
            return invalid("duplicate or empty id");
        if (clip.text.size() > kMaximumSequenceTextBytes || clip.text.find('\0') != std::string::npos)
            return invalid("text must be at most 4096 bytes");
        if (clip.tag.size() > kMaximumSequenceTextTagBytes || clip.tag.find('\0') != std::string::npos)
            return invalid("tag must be at most 63 bytes");
        if (!std::isfinite(clip.startSeconds) || !std::isfinite(clip.durationSeconds) || clip.startSeconds < 0.0f ||
            clip.durationSeconds <= 0.0f || clip.startSeconds + clip.durationSeconds > kMaxSequenceDurationSeconds)
            return invalid("invalid interval");
        if (!TEXT::IsNamedEnum(kLayoutNames, clip.layout) || !TEXT::IsNamedEnum(kAlignNames, clip.alignX) ||
            !TEXT::IsNamedEnum(kAlignNames, clip.alignY) || !TEXT::IsNamedEnum(kRevealNames, clip.reveal))
            return invalid("unknown layout, alignment or reveal");
        if (!InRange(clip.x, -1.0f, 2.0f) || !InRange(clip.y, -1.0f, 2.0f) || !InRange(clip.width, 0.01f, 4.0f) ||
            !InRange(clip.columnSpacing, 0.5f, 8.0f) || !InRange(clip.scale, 0.05f, 16.0f))
            return invalid("position, width, column spacing or scale out of range");
        if (!InRange(clip.tint.r, 0.0f, 4.0f) || !InRange(clip.tint.g, 0.0f, 4.0f) ||
            !InRange(clip.tint.b, 0.0f, 4.0f) || !InRange(clip.tint.a, 0.0f, 1.0f))
            return invalid("tint out of range");
        if (!InRange(clip.revealSeconds, 0.0f, kMaxSequenceDurationSeconds) ||
            !InRange(clip.fadeInSeconds, 0.0f, kMaxSequenceDurationSeconds) ||
            !InRange(clip.fadeOutSeconds, 0.0f, kMaxSequenceDurationSeconds))
            return invalid("invalid reveal or fade time");
        if (clip.order < -100000 || clip.order > 100000)
            return invalid("order must be within -100000..100000");
    }
    return true;
}

float GetSequenceTextContentEnd(const SequenceTextTrack& track) noexcept {
    float end = 0.0f;
    for (const auto& clip : track.clips)
        if (std::isfinite(clip.startSeconds) && std::isfinite(clip.durationSeconds))
            end = (std::max)(end, clip.startSeconds + clip.durationSeconds);
    return end;
}

float GetSequenceTextOpacity(const SequenceTextClip& clip, float localSeconds) noexcept {
    if (!std::isfinite(localSeconds) || localSeconds < 0.0f || localSeconds > clip.durationSeconds)
        return 0.0f;
    const float in = clip.fadeInSeconds > 0.0f ? localSeconds / clip.fadeInSeconds : 1.0f;
    const float out = clip.fadeOutSeconds > 0.0f ? (clip.durationSeconds - localSeconds) / clip.fadeOutSeconds : 1.0f;
    return std::clamp((std::min)(in, out), 0.0f, 1.0f);
}

float GetSequenceTextReveal(const SequenceTextClip& clip, float localSeconds) noexcept {
    if (clip.reveal != SequenceTextReveal::Typewriter || clip.revealSeconds <= 0.0f)
        return 1.0f;
    return std::isfinite(localSeconds) ? std::clamp(localSeconds / clip.revealSeconds, 0.0f, 1.0f) : 1.0f;
}

std::string GetSequenceTextLabel(const SequenceTextClip& clip) {
    std::string label = clip.text.substr(0, clip.text.find('\n'));
    if (!clip.tag.empty())
        label = "[" + clip.tag + "] " + label;
    return label;
}

const char* ToString(SequenceTextLayout value) noexcept { return TEXT::EnumToName(kLayoutNames, value, "horizontal"); }
const char* ToString(SequenceTextAlign value) noexcept { return TEXT::EnumToName(kAlignNames, value, "center"); }
const char* ToString(SequenceTextReveal value) noexcept { return TEXT::EnumToName(kRevealNames, value, "none"); }
bool ParseSequenceTextLayout(std::string_view text, SequenceTextLayout& out) noexcept {
    return TEXT::TryParseEnum(kLayoutNames, text, out);
}
bool ParseSequenceTextAlign(std::string_view text, SequenceTextAlign& out) noexcept {
    return TEXT::TryParseEnum(kAlignNames, text, out);
}
bool ParseSequenceTextReveal(std::string_view text, SequenceTextReveal& out) noexcept {
    return TEXT::TryParseEnum(kRevealNames, text, out);
}
} // namespace HIKARI::SEQUENCER
