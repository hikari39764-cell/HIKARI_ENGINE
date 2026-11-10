#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Editor/NodeGraph/HIKARI_NodeGraphCanvas.h"
#include "HIKARI/Project/EditorNodeGraph.h"

namespace HIKARI::EDITOR {

// 節・引脚・線を呼び手から毎フレーム受け取って描く節点図。ここが持つのは視点、選択、進行中の操作だけ。
// 編集の結果（節が動いた、線が引かれた、作成・削除・複写の要求）は outResult で返し、データは呼び手が書き換える。
// 実装は三つのファイル：操作（HIKARI_NodeGraphView.cpp）、配置と当たり判定（...Layout.cpp）、描画（...Paint.cpp）。
class NodeGraphView final {
public:
    // 今の ImGui 窓の中に描く。outResult の配列は次の Draw まで有効。
    void Draw(const char* stableId, const EDITOR_PROJECT::EditorNodeGraphDesc& desc,
              EDITOR_PROJECT::EditorNodeGraphResult& outResult);

private:
    enum class Action : uint8_t {
        None,
        Pan,
        MoveNodes,
        DragLink,
        BoxSelect,
    };
    enum class MenuTarget : uint8_t {
        Canvas,
        Node,
        Link,
    };

    struct NodeLayout {
        MATH::Vec2 position{};
        MATH::Vec2 size{};
        float headerHeight = 0.0f;
        float detailTop = 0.0f;
        float inputLabelTop = 0.0f;
        float outputLabelTop = 0.0f;
        uint32_t firstPin = 0u;
        bool selected = false;
    };
    struct PinLayout {
        const EDITOR_PROJECT::EditorNodeGraphPin* pin = nullptr;
        MATH::Vec2 position{};
        uint32_t node = 0u;
        bool output = false;
    };
    // 一フレームの画布の位置、ポインタ、その下にある物。
    struct Frame {
        MATH::Vec2 origin{};
        MATH::Vec2 size{};
        // 図の座標。
        MATH::Vec2 pointer{};
        bool hovered = false;
        bool focused = false;
        bool editable = false;
        int pin = -1;
        int node = -1;
        int link = -1;
    };

    // ---- 配置と当たり判定（HIKARI_NodeGraphViewLayout.cpp） ----
    void BuildLayout(const EDITOR_PROJECT::EditorNodeGraphDesc& desc);
    void FrameNodes(const MATH::Vec2& viewportSize, bool selectedOnly);
    const PinLayout* FindPin(uint64_t pinId) const;
    int HitPin(const MATH::Vec2& pointer, float radius) const;
    int HitNode(const MATH::Vec2& pointer) const;
    int HitLink(const EDITOR_PROJECT::EditorNodeGraphDesc& desc, const MATH::Vec2& pointer, float tolerance) const;
    std::array<MATH::Vec2, 4> LinkCurve(EDITOR_PROJECT::EditorNodeGraphFlow flow, const MATH::Vec2& start,
                                        const MATH::Vec2& end) const;

    // ---- 操作（HIKARI_NodeGraphView.cpp） ----
    void ApplyRequestedSelection(const EDITOR_PROJECT::EditorNodeGraphDesc& desc);
    void SelectNode(uint64_t nodeId, bool toggle, bool additive);
    void BeginAction(const EDITOR_PROJECT::EditorNodeGraphDesc& desc, const Frame& frame,
                     EDITOR_PROJECT::EditorNodeGraphResult& outResult);
    // 進行中の操作を進める。節の拖動が終わって呼び手へ渡す位置がある時に true。
    bool ContinueAction(const EDITOR_PROJECT::EditorNodeGraphDesc& desc, const Frame& frame,
                        EDITOR_PROJECT::EditorNodeGraphResult& outResult);
    void HandleKeys(const EDITOR_PROJECT::EditorNodeGraphDesc& desc, const Frame& frame,
                    EDITOR_PROJECT::EditorNodeGraphResult& outResult);
    void OpenMenu(MenuTarget target, const MATH::Vec2& position, uint64_t fromPinId);
    void DrawContextMenu(const EDITOR_PROJECT::EditorNodeGraphDesc& desc,
                         EDITOR_PROJECT::EditorNodeGraphResult& outResult);

    // ---- 描画（HIKARI_NodeGraphViewPaint.cpp） ----
    void Paint(const EDITOR_PROJECT::EditorNodeGraphDesc& desc, const Frame& frame) const;
    void PaintNode(const EDITOR_PROJECT::EditorNodeGraphDesc& desc, const Frame& frame, uint32_t index) const;

    NodeGraphCanvasState canvas_{};
    bool framed_ = false;
    // 視点を合わせた時の画布の大きさ。窓の大きさが落ち着くまで合わせ直すために覚える。
    MATH::Vec2 framedSize_{};
    Action action_ = Action::None;
    int panButton_ = 0;
    float panDistance_ = 0.0f;
    // 拖動中の節のずれ（図の座標）。離した時に一度だけ呼び手へ返す。
    MATH::Vec2 moveOffset_{};
    uint64_t dragPinId_ = 0u;
    MATH::Vec2 boxStart_{};
    bool boxAdditive_ = false;
    MenuTarget menuTarget_ = MenuTarget::Canvas;
    MATH::Vec2 menuPosition_{};
    uint64_t menuFromPinId_ = 0u;
    std::array<char, 64> menuFilter_{};

    std::vector<uint64_t> selectedNodes_{};
    std::vector<uint64_t> selectedLinks_{};

    // フレームごとに作り直す配置。
    std::vector<NodeLayout> nodes_{};
    std::vector<PinLayout> pins_{};
    std::unordered_map<uint64_t, uint32_t> pinById_{};
    std::vector<EDITOR_PROJECT::EditorNodeGraphMovedNode> moved_{};
    std::vector<EDITOR_PROJECT::EditorWorkspacePoint> sizes_{};
};

} // namespace HIKARI::EDITOR
