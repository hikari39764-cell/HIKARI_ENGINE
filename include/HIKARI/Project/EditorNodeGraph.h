#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/EditorWorkspace.h"

// 節点図の控件（IEditorWorkspaceUi::NodeGraph）。
// 節・引脚・線のデータと撤销はモジュールが持ち、毎フレーム今の内容を渡す。控件は描画、平移と拡縮、選択、
// 節の拖動、線引き、右クリックの献立、キー操作を受け持ち、起きたことを EditorNodeGraphResult で返す。
// 視点と選択は控件が stableGraphId ごとに覚える。
namespace HIKARI::EDITOR_PROJECT {

// 引脚の並ぶ向き。
enum class EditorNodeGraphFlow : uint32_t {
    // データの流れ：入力は節の左、出力は右。
    LeftToRight = 0u,
    // 樹：入力（親）は節の上、出力（子）は下。
    TopToBottom = 1u,
};

// 節と線の強調。Play 中の実行状態などを見せる。
enum class EditorNodeGraphHighlight : uint32_t {
    None = 0u,
    Active,
    Success,
    Failure,
    Warning,
};

struct EditorNodeGraphPin {
    // 図の中で一意（節の id とは別の番号空間）。0 は無効。
    uint64_t id = 0u;
    // UTF-8。無くてもよい。
    const char* label = nullptr;
    EditorWorkspaceColor color{0.78f, 0.80f, 0.84f, 1.0f};
    // 出力と入力の kind が同じ時だけ線を引ける。0 はどれとでも繋がる。
    uint32_t kind = 0u;
    uint32_t reserved = 0u;
};

struct EditorNodeGraphNode {
    // 図の中で一意。0 は無効。
    uint64_t id = 0u;
    const char* title = nullptr;
    // 題の下に出す小さい字（参数の要約など）。改行で複数行。無くてもよい。
    const char* detail = nullptr;
    // 題の右に出す短い字（実行の順番など）。無くてもよい。
    const char* badge = nullptr;
    // 図の座標での左上。
    EditorWorkspacePoint position{};
    // 題の帯の色。
    EditorWorkspaceColor color{0.24f, 0.36f, 0.52f, 1.0f};
    const EditorNodeGraphPin* inputs = nullptr;
    const EditorNodeGraphPin* outputs = nullptr;
    uint32_t inputCount = 0u;
    uint32_t outputCount = 0u;
    EditorNodeGraphHighlight highlight = EditorNodeGraphHighlight::None;
    uint32_t reserved = 0u;
};

struct EditorNodeGraphLink {
    // 図の中で一意（線の番号空間）。0 は無効。
    uint64_t id = 0u;
    uint64_t outputPinId = 0u;
    uint64_t inputPinId = 0u;
    EditorWorkspaceColor color{0.62f, 0.66f, 0.72f, 1.0f};
    EditorNodeGraphHighlight highlight = EditorNodeGraphHighlight::None;
    uint32_t reserved = 0u;
};

// 右クリックの献立にモジュールが出す項目：新しく作れる節の種類（createItems）と、節に対する操作（nodeActions）。
struct EditorNodeGraphMenuItem {
    const char* label = nullptr;
    // 献立の小見出し。無ければ直下に並ぶ（nodeActions では使わない）。
    const char* category = nullptr;
};

struct EditorNodeGraphDesc {
    uint32_t structSize = sizeof(EditorNodeGraphDesc);
    EditorNodeGraphFlow flow = EditorNodeGraphFlow::LeftToRight;
    const EditorNodeGraphNode* nodes = nullptr;
    const EditorNodeGraphLink* links = nullptr;
    // 何も無い所を右クリックした献立（と、引脚から線を引いて離した時の献立）に並ぶ、新しく作れる節の種類。
    const EditorNodeGraphMenuItem* createItems = nullptr;
    // 節を右クリックした献立に足す、モジュール自身の操作（「根にする」「無効にする」など）。選んでいる節が対象。
    // readOnly でも出すので、その時に許す操作だけを渡す。
    const EditorNodeGraphMenuItem* nodeActions = nullptr;
    // replaceSelection が 0 以外の時、選択をこの節に置き換える（貼り付けた節を選ぶなど）。
    const uint64_t* selectNodeIds = nullptr;
    uint32_t nodeCount = 0u;
    uint32_t linkCount = 0u;
    uint32_t createItemCount = 0u;
    uint32_t nodeActionCount = 0u;
    uint32_t selectNodeCount = 0u;
    uint32_t replaceSelection = 0u;
    // 0 以外で編集を受け付けない（見る・選ぶ・平移・拡縮だけ）。
    uint32_t readOnly = 0u;
    // 0 以外で、全部の節が収まるよう視点を合わせる（ファイルを開いた直後など）。
    uint32_t frameAll = 0u;
    // 控件の高さ。0 以下は窓の残り全部。
    float height = 0.0f;
    uint32_t reserved = 0u;
};

struct EditorNodeGraphMovedNode {
    uint64_t id = 0u;
    // 図の座標での新しい左上。
    EditorWorkspacePoint position{};
};

// 配列は次の NodeGraph 呼び出しか、フレームの終わりまで有効。
struct EditorNodeGraphResult {
    const uint64_t* selectedNodeIds = nullptr;
    const uint64_t* selectedLinkIds = nullptr;
    // 拖動が終わった節。一回の拖動につき一度だけ返る（途中の位置は控件が持つ）。
    const EditorNodeGraphMovedNode* movedNodes = nullptr;
    // desc.nodes と同じ並びの、図の座標での節の大きさ（自動整列用）。
    const EditorWorkspacePoint* nodeSizes = nullptr;
    uint32_t selectedNodeCount = 0u;
    uint32_t selectedLinkCount = 0u;
    uint32_t movedNodeCount = 0u;
    uint32_t nodeSizeCount = 0u;
    uint32_t selectionChanged = 0u;
    // 出力の引脚から入力の引脚へ線が引かれた。受け入れるか（循環、一本だけの入力など）はモジュールが決める。
    uint32_t linkCreated = 0u;
    uint64_t linkOutputPinId = 0u;
    uint64_t linkInputPinId = 0u;
    // 献立で選ばれた節の種類（desc.createItems の番号）。選ばれていなければ負。
    int32_t createItemIndex = -1;
    // 選んでいる節と線の削除（Delete キーか献立）。
    uint32_t deleteRequested = 0u;
    // 作る位置（図の座標）。
    EditorWorkspacePoint createPosition{};
    // 引脚から線を引いて何も無い所で離し、献立から作った時の、その引脚。それ以外は 0。
    uint64_t createFromPinId = 0u;
    // 複写・切り取り・複製は選んでいる節が対象。文字の受け渡しは SetClipboardText / GetClipboardText。
    uint32_t copyRequested = 0u;
    uint32_t cutRequested = 0u;
    uint32_t pasteRequested = 0u;
    uint32_t duplicateRequested = 0u;
    // 貼り付ける位置（図の座標）。
    EditorWorkspacePoint pastePosition{};
    // 二度押しされた節（無ければ 0）。
    uint64_t activatedNodeId = 0u;
    // ポインタの下の節（無ければ 0）。
    uint64_t hoveredNodeId = 0u;
    // 献立で選ばれたモジュールの操作（desc.nodeActions の番号）。選ばれていなければ負。
    int32_t nodeActionIndex = -1;
    uint32_t reserved = 0u;
};

static_assert(std::is_standard_layout_v<EditorNodeGraphPin> && std::is_trivially_copyable_v<EditorNodeGraphPin>);
static_assert(sizeof(EditorNodeGraphPin) == 40u);
static_assert(std::is_standard_layout_v<EditorNodeGraphNode> && std::is_trivially_copyable_v<EditorNodeGraphNode>);
static_assert(sizeof(EditorNodeGraphNode) == 88u);
static_assert(std::is_standard_layout_v<EditorNodeGraphLink> && std::is_trivially_copyable_v<EditorNodeGraphLink>);
static_assert(sizeof(EditorNodeGraphLink) == 48u);
static_assert(std::is_standard_layout_v<EditorNodeGraphMenuItem>);
static_assert(sizeof(EditorNodeGraphMenuItem) == 16u);
static_assert(std::is_standard_layout_v<EditorNodeGraphDesc> && std::is_trivially_copyable_v<EditorNodeGraphDesc>);
static_assert(sizeof(EditorNodeGraphDesc) == 88u);
static_assert(std::is_standard_layout_v<EditorNodeGraphMovedNode>);
static_assert(sizeof(EditorNodeGraphMovedNode) == 16u);
static_assert(std::is_standard_layout_v<EditorNodeGraphResult> && std::is_trivially_copyable_v<EditorNodeGraphResult>);
static_assert(sizeof(EditorNodeGraphResult) == 144u);

} // namespace HIKARI::EDITOR_PROJECT
