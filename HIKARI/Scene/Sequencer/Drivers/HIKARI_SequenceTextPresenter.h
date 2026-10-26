#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "HIKARI/Project/UiTypes.h"
#include "Scene/Sequencer/Tracks/HIKARI_SequenceTextTrack.h"

namespace HIKARI::GAME_UI {
class GameUiPresentationService;
}

namespace HIKARI::SEQUENCER {
class SequencePlaybackService;

// 文字段を画面の UI へ出す一回分（横書きは一塊、縦書きは一列）。request の色は样式の色に掛ける倍率。
struct SequenceTextDraw {
    std::string text;
    GameProjectUiTextRequest request{};
};

// 片段を演出の時刻 sequenceSeconds で描く依頼に分ける（見えない時刻なら何も足さない）。
// pixelHeight は样式の字の大きさ×scale、lineHeight は样式の行の高さの倍率。座標は 1920×1080 の基準画面。
void BuildSequenceTextDraws(const SequenceTextClip& clip, float sequenceSeconds, float pixelHeight, float lineHeight,
                            uint64_t sourceBase, std::vector<SequenceTextDraw>& out);

// 流れている演出（Play の再生と、Play の外の演出ワークスペースのプレビュー）の文字段を、今の時刻で World の UI へ
// 出す。UI の依頼はフレームごとに消えるので、演出が止まっていても毎フレーム呼ぶ。样式の無い片段は描かない。
void SubmitSequenceTexts(const SequencePlaybackService& playback, GAME_UI::GameUiPresentationService& ui);
} // namespace HIKARI::SEQUENCER
