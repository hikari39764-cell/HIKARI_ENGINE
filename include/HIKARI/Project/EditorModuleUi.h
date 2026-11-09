#pragma once

#include <memory>
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
    // ペンの筆圧（0..1）。マウスは常に 1。
    float pressure = 1.0f;
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

// 場面の撤销に載せる一件（内容は適用済み）。Undo / Redo は編集器の所有スレッドで呼ばれる。
class IEditorUndoEntry {
public:
    virtual ~IEditorUndoEntry() = default;
    virtual void Undo() = 0;
    virtual void Redo() = 0;
};

// 場面の主ファイルの保存が成功した後に呼ばれる。モジュールは場面に添えた内容をここで書く。
class IEditorSceneListener {
public:
    virtual ~IEditorSceneListener() = default;
    virtual void OnSceneSaved(std::string_view scenePathUtf8) = 0;
};

// モジュールが登録されている間有効。IEditorOperationRegistrar::SceneServices から受け取る。
class IEditorSceneServices {
public:
    virtual ~IEditorSceneServices() = default;
    virtual void RecordUndo(std::string_view label, std::unique_ptr<IEditorUndoEntry> entry) = 0;
    virtual void SetSceneListener(IEditorSceneListener* listener) = 0;
    virtual bool ActivateViewportTool(std::string_view toolId) = 0;
    // 模型から遠景模型を焼いて導入を予約し、その GUID を outFarModelGuid へ書く（終端込み）。理由はログへ。
    virtual bool BakeFarModel(std::string_view modelAssetGuid, char* outFarModelGuid, size_t capacity) = 0;
};

} // namespace HIKARI::EDITOR_PROJECT
