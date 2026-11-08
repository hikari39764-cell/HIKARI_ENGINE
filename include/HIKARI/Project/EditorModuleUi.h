#pragma once

#include <string_view>

#include "HIKARI/Project/EditorWorkspace.h"
#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI::EDITOR_PROJECT {

// 場景視口の中のポインタの状態。引擎が毎フレーム作って、有効な視口工具へ渡す。
struct EditorViewportInput {
    GameProjectFloat3 rayOrigin{};
    GameProjectFloat3 rayDirection{};
    // ポインタの光線と地平面（y = 0）の交点。地形などを持つモジュールは自分の面に当て直す。
    GameProjectFloat3 groundPoint{};
    bool hasGroundPoint = false;
    bool pointerInViewport = false;
    bool primaryPressed = false;
    bool primaryDown = false;
    bool primaryReleased = false;
    bool shift = false;
    bool control = false;
    bool alt = false;
    float wheel = 0.0f;
    float deltaSeconds = 0.0f;
};

// 視口工具が今のフレームに描く線と文字（筆の光標など）。座標は世界空間。
class IEditorViewportCanvas {
public:
    virtual ~IEditorViewportCanvas() = default;
    virtual void DrawLine(const GameProjectFloat3& from, const GameProjectFloat3& to, EditorWorkspaceColor color,
                          float thickness = 1.5f) = 0;
    // 水平な円（y が中心の高さの XZ 平面上）。
    virtual void DrawCircle(const GameProjectFloat3& center, float radius, EditorWorkspaceColor color,
                            float thickness = 1.5f) = 0;
    // 視口の左上に出す一行の案内。
    virtual void DrawHint(std::string_view utf8Text, EditorWorkspaceColor color) = 0;
};

// 場景視口でマウスを受け取る工具。有効な間は選択と変換の手柄より先にマウスを取る。一度に有効なのは一つ。
class IEditorViewportTool {
public:
    virtual ~IEditorViewportTool() = default;
    virtual const char* GetToolId() const noexcept = 0;
    virtual const char* GetDisplayName() const noexcept = 0;
    virtual void OnActivate() {}
    virtual void OnDeactivate() {}
    virtual void Update(const EditorViewportInput& input, IEditorViewportCanvas& canvas) = 0;
};

// 場景の作業区に出す窓。開閉は引擎が持ち、メニューと操作から切り替える。
class IEditorPanel {
public:
    virtual ~IEditorPanel() = default;
    virtual const char* GetPanelId() const noexcept = 0;
    virtual const char* GetDisplayName() const noexcept = 0;
    virtual void Draw(IEditorWorkspaceUi& ui) = 0;
};

} // namespace HIKARI::EDITOR_PROJECT
