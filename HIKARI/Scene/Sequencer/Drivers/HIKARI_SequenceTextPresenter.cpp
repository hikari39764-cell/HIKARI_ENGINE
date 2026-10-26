#include "HIKARI_SequenceTextPresenter.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"

namespace HIKARI::SEQUENCER {
namespace {
float AlignFactor(SequenceTextAlign align) noexcept {
    return align == SequenceTextAlign::Start ? 0.0f : align == SequenceTextAlign::Center ? 0.5f : 1.0f;
}

GameProjectUiTextAlign HorizontalAlign(SequenceTextAlign align) noexcept {
    return align == SequenceTextAlign::Start    ? GameProjectUiTextAlign::Left
           : align == SequenceTextAlign::Center ? GameProjectUiTextAlign::Center
                                                : GameProjectUiTextAlign::Right;
}

GameProjectUiVerticalAlign VerticalAlign(SequenceTextAlign align) noexcept {
    return align == SequenceTextAlign::Start    ? GameProjectUiVerticalAlign::Top
           : align == SequenceTextAlign::Center ? GameProjectUiVerticalAlign::Center
                                                : GameProjectUiVerticalAlign::Bottom;
}

size_t CountCodepoints(std::string_view text) noexcept {
    return static_cast<size_t>(std::count_if(text.begin(), text.end(),
                                             [](char c) { return (static_cast<unsigned char>(c) & 0xC0u) != 0x80u; }));
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines(1);
    for (const char c : text) {
        if (c == '\n')
            lines.emplace_back();
        else if (c != '\r')
            lines.back().push_back(c);
    }
    return lines;
}

GameProjectUiTextRequest BaseRequest(const SequenceTextClip& clip, float opacity, float reveal, float pixelHeight) {
    GameProjectUiTextRequest request{};
    request.order = clip.order;
    request.referenceWidth = kSequenceTextReferenceWidth;
    request.referenceHeight = kSequenceTextReferenceHeight;
    request.pixelHeight = pixelHeight;
    request.revealAmount = reveal;
    request.wrap = GameProjectUiTextWrap::Character;
    request.textColor = {clip.tint.r, clip.tint.g, clip.tint.b, clip.tint.a * opacity};
    return request;
}
} // namespace

void BuildSequenceTextDraws(const SequenceTextClip& clip, float sequenceSeconds, float pixelHeight, float lineHeight,
                            uint64_t sourceBase, std::vector<SequenceTextDraw>& out) {
    const float local = sequenceSeconds - clip.startSeconds;
    const float opacity = GetSequenceTextOpacity(clip, local);
    if (opacity <= 0.0f || clip.text.empty() || !std::isfinite(pixelHeight) || pixelHeight <= 0.0f)
        return;
    const float reveal = GetSequenceTextReveal(clip, local);
    const float anchorX = clip.x * kSequenceTextReferenceWidth;
    const float anchorY = clip.y * kSequenceTextReferenceHeight;
    const float ax = AlignFactor(clip.alignX), ay = AlignFactor(clip.alignY);
    if (clip.layout == SequenceTextLayout::Horizontal) {
        // 高さは折り返しで決まるので、置き場所を合わせる側の端に揃えた十分に高い箱に入れる。
        const float width = clip.width * kSequenceTextReferenceWidth;
        const float height = kSequenceTextReferenceHeight * 2.0f;
        SequenceTextDraw draw{clip.text, BaseRequest(clip, opacity, reveal, pixelHeight)};
        draw.request.sourceId = sourceBase;
        draw.request.x = anchorX - width * ax;
        draw.request.y = anchorY - height * ay;
        draw.request.maxWidth = width;
        draw.request.maxHeight = height;
        draw.request.horizontalAlign = HorizontalAlign(clip.alignX);
        draw.request.verticalAlign = VerticalAlign(clip.alignY);
        out.push_back(std::move(draw));
        return;
    }
    // 縦書き：一行を一列にし、一字ごとに折り返す細い箱で上から下へ。列は右から左へ並べる。
    const std::vector<std::string> columns = SplitLines(clip.text);
    std::vector<size_t> counts;
    size_t total = 0u, longest = 0u;
    for (const auto& column : columns) {
        counts.push_back(CountCodepoints(column));
        total += counts.back();
        longest = (std::max)(longest, counts.back());
    }
    if (total == 0u)
        return;
    const float advance = pixelHeight * (std::max)(lineHeight, 0.1f);
    const float columnWidth = pixelHeight * 1.25f;
    const float pitch = pixelHeight * clip.columnSpacing;
    const float boxWidth = columnWidth + pitch * static_cast<float>(columns.size() - 1u);
    const float boxHeight = pixelHeight + advance * static_cast<float>(longest - 1u);
    const float right = anchorX - boxWidth * ax + boxWidth;
    const float top = anchorY - boxHeight * ay;
    const float revealed = reveal * static_cast<float>(total);
    size_t before = 0u;
    for (size_t index = 0u; index < columns.size(); ++index) {
        const size_t count = counts[index];
        const float shown =
            count ? std::clamp((revealed - static_cast<float>(before)) / static_cast<float>(count), 0.0f, 1.0f) : 0.0f;
        before += count;
        if (shown <= 0.0f)
            continue;
        SequenceTextDraw draw{columns[index], BaseRequest(clip, opacity, shown, pixelHeight)};
        draw.request.sourceId = sourceBase + index;
        draw.request.x = right - columnWidth - pitch * static_cast<float>(index);
        draw.request.y = top;
        draw.request.maxWidth = columnWidth;
        draw.request.maxHeight = boxHeight;
        draw.request.horizontalAlign = GameProjectUiTextAlign::Center;
        draw.request.verticalAlign = VerticalAlign(clip.alignY);
        out.push_back(std::move(draw));
    }
}

void SubmitSequenceTexts(const SequencePlaybackService& playback, GAME_UI::GameUiPresentationService& ui) {
    std::vector<SequenceTextDraw> draws;
    for (const SequencePlaybackHandle handle : playback.GetActiveHandles()) {
        const CinematicSequence* sequence = playback.GetSequence(handle);
        if (sequence == nullptr)
            continue;
        const float time = playback.GetTimeSeconds(handle);
        for (const auto& clip : sequence->textTrack.clips) {
            if (!clip.style.IsValid() || time < clip.startSeconds || time > clip.startSeconds + clip.durationSeconds)
                continue;
            const auto style = ui.FindTextStyle(clip.style);
            if (!style)
                continue;
            draws.clear();
            // UI の依頼の識別：実例と片段と列から作る（同じ文字が別の演出で同時に出ても混ざらない）。
            const uint64_t source = 0x5345515445585400ull ^ (handle.value << 24u) ^ (clip.id << 8u);
            BuildSequenceTextDraws(clip, time, style->pixelHeight * clip.scale, style->effects.lineHeight, source,
                                   draws);
            for (const auto& draw : draws)
                (void)ui.SubmitTextStyle(clip.style, draw.text, draw.request, clip.scale);
        }
    }
}
} // namespace HIKARI::SEQUENCER
