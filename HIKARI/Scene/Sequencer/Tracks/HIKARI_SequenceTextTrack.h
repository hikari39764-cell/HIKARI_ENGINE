#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "HIKARI/Project/UiTypes.h"

namespace HIKARI::SEQUENCER {
// 文字轨：演出の時刻に画面へ文字を出す（字幕、語り、題、縦書きの巻物の詞書など）。
// 字体・大きさ・色・縁取り・影は文字样式资产（TextStyle）から取り、片段は置き場所・並べ方・現れ方だけを持つ。
// 引擎が Play でも演出ワークスペースのプレビューでも同じように描く（SequenceTextPresenter）。
// 样式が無い片段は引擎は描かず、ゲームのプロジェクトが SDK の SequenceView::GetTextClips で読んで自分で描ける。
enum class SequenceTextLayout : uint8_t {
    Horizontal, // 横書き。width の幅で折り返す
    Vertical,   // 縦書き。一行を一列にして右の列から左へ並べる
};
// 置き場所 (x, y) に文字の箱のどこを合わせるか（横は 左・中・右、縦は 上・中・下）。
enum class SequenceTextAlign : uint8_t { Start, Center, End };
enum class SequenceTextReveal : uint8_t {
    None,       // 淡入だけ
    Typewriter, // revealSeconds の間に先頭から一字ずつ現れる（縦書きは右の列から）
};

struct SequenceTextClip {
    uint64_t id = 1u;
    // UTF-8。改行で行を分ける（縦書きは改行ごとに列を変える）。
    std::string text = "文字";
    // ゲームのプロジェクトが読む印（装飾を足す片段の見分けなど）。引擎は使わない。
    std::string tag;
    float startSeconds = 0.0f;
    float durationSeconds = 3.0f;
    AssetGuid style;
    SequenceTextLayout layout = SequenceTextLayout::Horizontal;
    SequenceTextAlign alignX = SequenceTextAlign::Center;
    SequenceTextAlign alignY = SequenceTextAlign::End;
    // 置き場所は 1920×1080 の基準画面（画面に収めて中央に置く）の割合。
    float x = 0.5f;
    float y = 0.9f;
    // 横書きの折り返しの幅（基準画面の幅の割合）。
    float width = 0.8f;
    // 縦書きの列の間隔（字の大きさの倍数）。
    float columnSpacing = 1.6f;
    // 样式の字の大きさに掛ける。
    float scale = 1.0f;
    // 样式の色に掛ける（a は全体の不透明度）。
    GameProjectUiColor tint{1.0f, 1.0f, 1.0f, 1.0f};
    SequenceTextReveal reveal = SequenceTextReveal::None;
    float revealSeconds = 1.0f;
    float fadeInSeconds = 0.3f;
    float fadeOutSeconds = 0.3f;
    // 画面の UI の重なり順（大きいほど上）。
    int32_t order = 100;
};

struct SequenceTextTrack {
    std::vector<SequenceTextClip> clips;
};

inline constexpr size_t kMaximumSequenceTexts = 256u;
inline constexpr size_t kMaximumSequenceTextBytes = 4096u;
// 印は SDK の固定長の欄（終端を含め 64 バイト）に収まる長さまで。
inline constexpr size_t kMaximumSequenceTextTagBytes = 63u;
inline constexpr float kSequenceTextReferenceWidth = 1920.0f;
inline constexpr float kSequenceTextReferenceHeight = 1080.0f;

bool ValidateSequenceTextTrack(const SequenceTextTrack& track, std::string& error);
float GetSequenceTextContentEnd(const SequenceTextTrack& track) noexcept;
// 片段の中の時刻 localSeconds での不透明度（0〜1、淡入と淡出）。
float GetSequenceTextOpacity(const SequenceTextClip& clip, float localSeconds) noexcept;
// 片段の中の時刻 localSeconds で現れている字の割合（0〜1）。
float GetSequenceTextReveal(const SequenceTextClip& clip, float localSeconds) noexcept;
// 時間轴などに出す短い名前：最初の行（印があれば頭に付ける）。
std::string GetSequenceTextLabel(const SequenceTextClip& clip);

const char* ToString(SequenceTextLayout value) noexcept;
const char* ToString(SequenceTextAlign value) noexcept;
const char* ToString(SequenceTextReveal value) noexcept;
bool ParseSequenceTextLayout(std::string_view text, SequenceTextLayout& out) noexcept;
bool ParseSequenceTextAlign(std::string_view text, SequenceTextAlign& out) noexcept;
bool ParseSequenceTextReveal(std::string_view text, SequenceTextReveal& out) noexcept;
} // namespace HIKARI::SEQUENCER
