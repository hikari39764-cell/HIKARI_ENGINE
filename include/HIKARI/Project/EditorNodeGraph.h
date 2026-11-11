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
    // 走っている（太い明るい縁）。
    Active,
    Success,
    Failure,
    Warning,
    // 子を待っている（細い明るい縁：この道を通っている）。
    Waiting,
    // 途中で打ち切られた（橙）。
    Aborted,
};

// 題の左に線で描く印（字体に頼らない）。
enum class EditorNodeGraphIcon : uint32_t {
    None = 0u,
    // 横線三本（子を順に扱う）。
    Composite,
    // 菱形（子を一つ包む）。
    Decorator,
    // 右向きの三角（何かをする）。
    Action,
    // 丸と中の点（確かめる）。
    Condition,
    // 重なった二つの四角（別の図を埋め込む）。
    Subtree,
};

// 節の右上の問題の印（実行の強調とは別に出る）。
enum class EditorNodeGraphProblem : uint32_t {
    None = 0u,
    Warning,
    Error,
};

// 線の形。
enum class EditorNodeGraphLinkStyle : uint32_t {
    // 滑らかな曲線（データの流れ向き）。
    Curve = 0u,
    // 直角に折れる線（樹）：出力から下へ、横へ、入力へ。
    Elbow = 1u,
};

// 節の飾り（EditorNodeGraphNode::flags）。
enum class EditorNodeGraphNodeFlag : uint32_t {
    None = 0u,
    // 下に畳む三角を出す（押すと EditorNodeGraphResult::toggledNodeId）。
    Collapsible = 1u << 0u,
    // 畳んである（三角の向きと、hiddenCount の数を出す）。
    Collapsed = 1u << 1u,
    // 止めてある（灰色に沈めて描く）。
    Disabled = 1u << 2u,
};

// 節を作る献立が何のために開いたか（EditorNodeGraphResult::createMode）。
enum class EditorNodeGraphCreateMode : uint32_t {
    // 何も無い所（createPosition に置く）。
    Place = 0u,
    // 選んでいる節の子に足す（Tab）。
    AddChild,
    // 選んでいる節と親の間に挟む（Shift+Tab）。
    Wrap,
    // 選んでいる節の種類を替える。
    Replace,
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

// 節の本体の一行：左に名前、右に値。capsule が 0 以外なら値を丸い札にする（変数を指す値など）。
struct EditorNodeGraphRow {
    const char* label = nullptr;
    const char* value = nullptr;
    uint32_t capsule = 0u;
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
    // 節の左上の外に出す順番（兄弟の中の実行順など）。0 は出さない。
    uint32_t order = 0u;
    // EditorNodeGraphNodeFlag の組み合わせ。
    uint32_t flags = 0u;
    // 畳んだ節の下に隠れている節の数（Collapsed の時に出す）。
    uint32_t hiddenCount = 0u;
    EditorNodeGraphIcon icon = EditorNodeGraphIcon::None;
    EditorNodeGraphProblem problem = EditorNodeGraphProblem::None;
    // 強調の濃さ（0..1。結果が出た後に薄れていく時など）。
    float highlightStrength = 1.0f;
    // 0..1 の進み具合を題の帯の下に棒で出す。負なら出さない。
    float progress = -1.0f;
    // 題の下の小さい字（種類の名前など、一行）。
    const char* subtitle = nullptr;
    // 節の一番下の注（二行まで）。
    const char* note = nullptr;
    // ポインタを乗せた時の説明（全文、種類の説明、問題）。
    const char* tooltip = nullptr;
    // 名前を変えられる節：変え始める時の字（空でよい）。nullptr なら変えられない。F2 か題の二度押しで変え始め、
    // Enter で EditorNodeGraphResult::renamedNodeId / renameText を返す（Esc で取り消し）。
    const char* editName = nullptr;
    // 進み具合の棒の横の字（「0.8 / 1.5 秒」）。
    const char* progressText = nullptr;
    // detail の下に並べる名前と値の行。
    const EditorNodeGraphRow* rows = nullptr;
    uint32_t rowCount = 0u;
    uint32_t reserved = 0u;
};

struct EditorNodeGraphLink {
    // 図の中で一意（線の番号空間）。0 は無効。
    uint64_t id = 0u;
    uint64_t outputPinId = 0u;
    uint64_t inputPinId = 0u;
    EditorWorkspaceColor color{0.62f, 0.66f, 0.72f, 1.0f};
    EditorNodeGraphHighlight highlight = EditorNodeGraphHighlight::None;
    // 0 以外：今通っている道。太くし、流れる点を描く。
    uint32_t flowing = 0u;
};

// 右クリックの献立にモジュールが出す項目：新しく作れる節の種類（createItems）と、節に対する操作（nodeActions）。
struct EditorNodeGraphMenuItem {
    const char* label = nullptr;
    // 献立の小見出し。無ければ直下に並ぶ（nodeActions では使わない）。
    const char* category = nullptr;
    // 絞り込みで label の他に当てる字（種類の型名、説明など。英字は大文字小文字を区別しない）。
    const char* keywords = nullptr;
    // createItems：出す献立（1 << EditorNodeGraphCreateMode の組み合わせ）。0 はどの献立にも出す。
    uint32_t modes = 0u;
    uint32_t reserved = 0u;
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
    EditorNodeGraphLinkStyle linkStyle = EditorNodeGraphLinkStyle::Curve;
    // 0 以外：節を拖動すると、線で繋がった下流の節（樹なら子孫）も一緒に動く。
    uint32_t dragDescendants = 0u;
    // 0 より大きい時、節の幅をこの刻みに切り上げて揃える（図の座標）。
    float nodeWidthStep = 0.0f;
    // 拖動中の落とし先の提示（呼び手が前のフレームの EditorNodeGraphResult から決める）。dropTargetNodeId の節を囲み、
    // dropLineFrom → dropLineTo（図の座標、同じ点なら無し）に挿入線を引き、dropMessage をポインタの横に出す。
    // dropBlocked が 0 以外なら禁止の色。
    uint64_t dropTargetNodeId = 0u;
    EditorWorkspacePoint dropLineFrom{};
    EditorWorkspacePoint dropLineTo{};
    const char* dropMessage = nullptr;
    uint32_t dropBlocked = 0u;
    // 0 より大きい時の節の幅の上限。収まらない字は「…」で切り、全文は tooltip で見せる。
    float nodeMaxWidth = 0.0f;
    // 0 以外：この節が画面の外にあれば、見える所まで視点を動かす（走っている節を追う時など）。
    uint64_t followNodeId = 0u;
    // 使える作る献立（1 << EditorNodeGraphCreateMode）。AddChild / Wrap / Replace を入れると、節の右クリックの献立に
    // 「子を足す…」「挟む…」「置き換える…」が並び、Tab と Shift+Tab で選んでいる節の横に開く。
    uint32_t createModes = 0u;
    uint32_t reserved2 = 0u;
    // 面板の項目から引いてきた物（IEditorWorkspaceUi::DragSource の payloadType）を受け取る。無ければ受け取らない。
    const char* dropPayloadType = nullptr;
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
    // 拖動で離された（dragDescendants の時）。拖動を Esc で取り消した時は 0 のまま拖動が終わる。
    uint32_t dropped = 0u;
    // 拖動中（と離したフレーム）に掴んでいる節と、ポインタの位置（図の座標）。拖動していなければ 0。
    uint64_t draggingNodeId = 0u;
    EditorWorkspacePoint dragPointer{};
    // 畳む三角が押された節（無ければ 0）。
    uint64_t toggledNodeId = 0u;
    // 名前を変え終えた節と、新しい名前（空なら名前を消す）。無ければ 0。
    uint64_t renamedNodeId = 0u;
    const char* renameText = nullptr;
    // createItemIndex を選んだ献立（AddChild / Wrap / Replace の対象は選んでいる節）。
    EditorNodeGraphCreateMode createMode = EditorNodeGraphCreateMode::Place;
    uint32_t reserved = 0u;
    // 面板から引いてきて図の上で離された物（DragSource の payload）。無ければ nullptr。
    const char* droppedPayload = nullptr;
    // 離した所の節と線（無ければ 0）と、図の座標。
    uint64_t dropPayloadNodeId = 0u;
    uint64_t dropPayloadLinkId = 0u;
    EditorWorkspacePoint dropPayloadPosition{};
};

static_assert(std::is_standard_layout_v<EditorNodeGraphPin> && std::is_trivially_copyable_v<EditorNodeGraphPin>);
static_assert(sizeof(EditorNodeGraphPin) == 40u);
static_assert(std::is_standard_layout_v<EditorNodeGraphNode> && std::is_trivially_copyable_v<EditorNodeGraphNode>);
static_assert(std::is_standard_layout_v<EditorNodeGraphRow> && std::is_trivially_copyable_v<EditorNodeGraphRow>);
static_assert(sizeof(EditorNodeGraphRow) == 24u);
static_assert(sizeof(EditorNodeGraphNode) == 168u);
static_assert(std::is_standard_layout_v<EditorNodeGraphLink> && std::is_trivially_copyable_v<EditorNodeGraphLink>);
static_assert(sizeof(EditorNodeGraphLink) == 48u);
static_assert(std::is_standard_layout_v<EditorNodeGraphMenuItem>);
static_assert(sizeof(EditorNodeGraphMenuItem) == 32u);
static_assert(std::is_standard_layout_v<EditorNodeGraphDesc> && std::is_trivially_copyable_v<EditorNodeGraphDesc>);
static_assert(sizeof(EditorNodeGraphDesc) == 160u);
static_assert(std::is_standard_layout_v<EditorNodeGraphMovedNode>);
static_assert(sizeof(EditorNodeGraphMovedNode) == 16u);
static_assert(std::is_standard_layout_v<EditorNodeGraphResult> && std::is_trivially_copyable_v<EditorNodeGraphResult>);
static_assert(sizeof(EditorNodeGraphResult) == 224u);

} // namespace HIKARI::EDITOR_PROJECT
