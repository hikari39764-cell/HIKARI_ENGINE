#pragma once

#include <cstdint>

#include "Core/Text/HIKARI_EnumNames.h"

struct ImDrawList;

namespace HIKARI {
class Camera3D;
}

namespace HIKARI::EDITOR {

// 画幅。Free 以外は、その比率の枠の外を暗くして、ほかの補助線を枠の中に描く。
enum class ViewFrameAspect : uint8_t {
    Free,
    Widescreen,
    Cinemascope,
    Flat,
    Classic,
    Square,
    Vertical,
};

inline constexpr TEXT::EnumName<ViewFrameAspect> kViewFrameAspectNames[] = {
    {ViewFrameAspect::Free, "free"},          {ViewFrameAspect::Widescreen, "16:9"},
    {ViewFrameAspect::Cinemascope, "2.39:1"}, {ViewFrameAspect::Flat, "1.85:1"},
    {ViewFrameAspect::Classic, "4:3"},        {ViewFrameAspect::Square, "1:1"},
    {ViewFrameAspect::Vertical, "9:16"},
};

// 構図の補助線。視口の上に ImGui で重ねるだけで、描画結果や成片には入らない。視口（EditorViewInstance や場面の
// 視口）ごとに持ち、工作区ごとに開閉を覚える。
struct EditorViewGuides {
    bool thirds = false;
    bool goldenRatio = false;
    bool center = false;
    // 動作の安全枠 90% と字幕の安全枠 80%。
    bool safeFrames = false;
    // 水平線（カメラの高さの消失線）。傾きやあおりの確認に使う。
    bool horizon = false;
    ViewFrameAspect aspect = ViewFrameAspect::Free;
};

// rect は画面座標の視口。camera は水平線に使う（無ければ水平線を描かない）。
void DrawEditorViewGuides(ImDrawList& drawList, float x, float y, float width, float height,
                          const EditorViewGuides& guides, const Camera3D* camera);
// 補助線の開閉（ポップアップの中身）。変えたら true。
bool DrawEditorViewGuidesMenu(EditorViewGuides& guides);
// 視口の工具列に置く「構図」ボタンとそのポップアップ。
void DrawEditorViewGuidesButton(const char* id, EditorViewGuides& guides);

} // namespace HIKARI::EDITOR
