#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

// Sequence playback is a runtime-only service. These fixed buffers keep
// authored identifiers and diagnostics on the value side of the public
// SDK boundary; the engine never returns an allocator-owned string.
inline constexpr uint32_t kGameProjectSequencePlaybackVersion = 1u;
inline constexpr uint32_t kGameProjectSequenceAssetGuidCapacity = 33u;
inline constexpr uint32_t kGameProjectSequenceChannelCapacity = 64u;
inline constexpr uint32_t kGameProjectSequenceSlotNameCapacity = 64u;
inline constexpr uint32_t kGameProjectSequenceMessageCapacity = 256u;
inline constexpr uint32_t kGameProjectSequenceMaximumBindings = 32u;
inline constexpr uint32_t kGameProjectSequencePlayOptionsV1Size = 112u;
// A GameProject camera-marker component may use these canonical property
// ids to let the Cinematics workspace restore an external Sequence's
// editor-only preview camera. Runtime playback remains explicit through
// SequenceView; the editor hint never auto-plays the asset.
inline constexpr char kGameProjectSequencePreviewAssetGuidProperty[] = "sequenceAssetGuid";
inline constexpr char kGameProjectSequencePreviewSlotNameProperty[] = "sequenceSlotName";
// 相機とは別の設定オブジェクトからも、型付き SceneObject 参照で同じプレビューを復元する。
inline constexpr char kGameProjectSequencePreviewCameraProperty[] = "cameraObject";

struct GameProjectSequencePlaybackHandle {
    uint64_t value = 0u;
    uint64_t epoch = 0u;

    constexpr bool IsValid() const noexcept { return value != 0u && epoch != 0u; }

    friend constexpr bool operator==(GameProjectSequencePlaybackHandle lhs,
                                     GameProjectSequencePlaybackHandle rhs) noexcept {
        return lhs.value == rhs.value && lhs.epoch == rhs.epoch;
    }

    friend constexpr bool operator!=(GameProjectSequencePlaybackHandle lhs,
                                     GameProjectSequencePlaybackHandle rhs) noexcept {
        return !(lhs == rhs);
    }
};

enum class GameProjectSequencePlaybackState : uint8_t {
    Stopped = 0u,
    Playing = 1u,
    Paused = 2u,
};

enum class GameProjectSequencePlaybackEventKind : uint8_t {
    Started = 0u,
    Paused = 1u,
    Resumed = 2u,
    Seeked = 3u,
    Completed = 4u,
    Stopped = 5u,
    Rejected = 6u,
};

enum class GameProjectSequenceStopReason : uint8_t {
    Stopped = 0u,
    Replaced = 1u,
    Completed = 2u,
    Disabled = 3u,
    OwnerDestroyed = 4u,
    Reset = 5u,
};

enum class GameProjectSequenceChannelPolicy : uint8_t {
    Parallel = 0u,
    Replace = 1u,
    ReplaceIfHigherOrEqual = 2u,
    RejectIfOccupied = 3u,
};

// A slot override is borrowed only during SequenceView::Play. The
// referenced object must be a live GameObject in the same World with a
// World-unique SceneObjectId, including an object from an instantiated
// Prefab; the engine resolves it to that identity before returning.
struct GameProjectSequenceBinding {
    char slotName[kGameProjectSequenceSlotNameCapacity]{};
    GameProjectObjectHandle object{};
};

// V1 is an extensible value descriptor. The pointer and its array are
// borrowed only for the synchronous Play call and are never retained by
// the engine after it returns.
struct GameProjectSequencePlayOptions {
    uint32_t structSize = kGameProjectSequencePlayOptionsV1Size;
    uint32_t version = kGameProjectSequencePlaybackVersion;
    float startTimeSeconds = 0.0f;
    // Signed playback rate in [-8,-0.01] or [0.01,8]. Negative values
    // evaluate the same asset backwards from startTimeSeconds.
    float playbackRate = 1.0f;
    int32_t priority = 0;
    GameProjectSequenceChannelPolicy channelPolicy = GameProjectSequenceChannelPolicy::ReplaceIfHigherOrEqual;
    uint8_t loop = 0u;
    // Keep the evaluated camera/track state active at the reached edge.
    // The playback remains paused and may be resumed after changing rate.
    uint8_t holdAtEnd = 0u;
    uint8_t reserved0 = 0u;
    char channel[kGameProjectSequenceChannelCapacity]{};
    const GameProjectSequenceBinding* bindings = nullptr;
    uint32_t bindingCount = 0u;
    uint32_t reserved[3] = {0u, 0u, 0u};
};

// Snapshot strings are copied into the caller's value. The snapshot is
// valid only for the call and becomes unavailable once the handle is no
// longer active or the owning World is rebuilt.
struct GameProjectSequencePlaybackSnapshot {
    GameProjectSequencePlaybackHandle handle{};
    GameProjectSequencePlaybackState state = GameProjectSequencePlaybackState::Stopped;
    uint8_t reserved0[3] = {0u, 0u, 0u};
    float timeSeconds = 0.0f;
    float durationSeconds = 0.0f;
    int32_t priority = 0;
    char assetGuid[kGameProjectSequenceAssetGuidCapacity]{};
    char channel[kGameProjectSequenceChannelCapacity]{};
    uint8_t reserved[7] = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
};

// 演出の事件段（演出ワークスペースの事件轨）：名前と範囲だけを写す。引擎は事件段を何も実行せず、
// プロジェクトが名前で読んで自分の効果を動かす。名前は UTF-8、終端を含めて 64 バイトまで。
inline constexpr uint32_t kGameProjectSequenceEventNameCapacity = 64u;
inline constexpr uint32_t kGameProjectSequenceMaximumEventClips = 128u;
struct GameProjectSequenceEventClip {
    char name[kGameProjectSequenceEventNameCapacity]{};
    float startSeconds = 0.0f;
    float durationSeconds = 0.0f;
};

// 演出の文字段（演出ワークスペースの文字轨）。引擎は文字样式のある片段を Play とプレビューで自分で描く。
// プロジェクトはこれを読んで装飾を足したり、样式の無い片段を自分で描いたりする。置き場所は 1920×1080 の
// 基準画面（画面に収めて中央に置く）の割合。文字そのものは GetTextClipText で読む（UTF-8、最大 4096 バイト）。
inline constexpr uint32_t kGameProjectSequenceTextTagCapacity = 64u;
inline constexpr uint32_t kGameProjectSequenceMaximumTextClips = 256u;
inline constexpr uint32_t kGameProjectSequenceTextMaximumBytes = 4096u;
inline constexpr float kGameProjectSequenceTextReferenceWidth = 1920.0f;
inline constexpr float kGameProjectSequenceTextReferenceHeight = 1080.0f;

enum class GameProjectSequenceTextLayout : uint8_t {
    Horizontal = 0u, // width で折り返す横書き
    Vertical = 1u,   // 一行を一列にし、右の列から左へ並べる縦書き
};
// 置き場所 (x, y) に文字の箱のどこを合わせるか（横は 左・中・右、縦は 上・中・下）。
enum class GameProjectSequenceTextAlign : uint8_t { Start = 0u, Center = 1u, End = 2u };
enum class GameProjectSequenceTextReveal : uint8_t {
    None = 0u,
    Typewriter = 1u, // revealSeconds の間に先頭から一字ずつ現れる
};

struct GameProjectSequenceTextClip {
    uint64_t id = 0u;
    float startSeconds = 0.0f;
    float durationSeconds = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;         // 横書きの折り返しの幅（基準画面の幅の割合）
    float columnSpacing = 0.0f; // 縦書きの列の間隔（字の大きさの倍数）
    float scale = 1.0f;
    float pixelHeight = 0.0f;   // 样式の字の大きさ×scale（基準画面の画素）。样式が読めなければ 0
    float lineHeight = 0.0f;    // 样式の行の高さの倍率（縦書きの字の送り）
    float revealSeconds = 0.0f;
    float fadeInSeconds = 0.0f;
    float fadeOutSeconds = 0.0f;
    GameProjectFloat4 tint{1.0f, 1.0f, 1.0f, 1.0f}; // 样式の色に掛ける（w は不透明度）
    int32_t order = 0;                               // UI の重なり順
    uint32_t textBytes = 0u;                         // 文字の UTF-8 のバイト数（終端を含まない）
    GameProjectSequenceTextLayout layout = GameProjectSequenceTextLayout::Horizontal;
    GameProjectSequenceTextAlign alignX = GameProjectSequenceTextAlign::Center;
    GameProjectSequenceTextAlign alignY = GameProjectSequenceTextAlign::End;
    GameProjectSequenceTextReveal reveal = GameProjectSequenceTextReveal::None;
    uint8_t engineDrawn = 0u; // 1 なら引擎がこの片段を描く（样式が読める）
    uint8_t reserved0[3] = {0u, 0u, 0u};
    char tag[kGameProjectSequenceTextTagCapacity]{};
    char styleGuid[kGameProjectSequenceAssetGuidCapacity]{};
    uint8_t reserved[7] = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
};

// Event values are delivered synchronously by ConsumeEvents. The event
// pointer passed to the callback is borrowed only for that callback. A
// message longer than the public buffer is truncated and marked, so a
// rejected request can never disappear because of diagnostic length.
struct GameProjectSequencePlaybackEvent {
    GameProjectSequencePlaybackHandle handle{};
    uint64_t requestId = 0u;
    float timeSeconds = 0.0f;
    GameProjectSequencePlaybackEventKind kind = GameProjectSequencePlaybackEventKind::Rejected;
    GameProjectSequenceStopReason stopReason = GameProjectSequenceStopReason::Stopped;
    uint8_t messageTruncated = 0u;
    uint8_t reserved0 = 0u;
    char assetGuid[kGameProjectSequenceAssetGuidCapacity]{};
    char message[kGameProjectSequenceMessageCapacity]{};
    uint8_t reserved[7] = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
};

using GameProjectSequencePlaybackEventCallback = void (*)(void* context,
                                                          const GameProjectSequencePlaybackEvent* event) noexcept;

static_assert(std::is_standard_layout_v<GameProjectSequencePlaybackHandle>);
static_assert(std::is_trivially_copyable_v<GameProjectSequencePlaybackHandle>);
static_assert(sizeof(GameProjectSequencePlaybackHandle) == 16u);
static_assert(offsetof(GameProjectSequencePlaybackHandle, value) == 0u);
static_assert(offsetof(GameProjectSequencePlaybackHandle, epoch) == 8u);
static_assert(std::is_standard_layout_v<GameProjectSequenceBinding>);
static_assert(std::is_trivially_copyable_v<GameProjectSequenceBinding>);
static_assert(sizeof(GameProjectSequenceBinding) == 80u);
static_assert(offsetof(GameProjectSequenceBinding, slotName) == 0u);
static_assert(offsetof(GameProjectSequenceBinding, object) == 64u);
static_assert(std::is_standard_layout_v<GameProjectSequencePlayOptions>);
static_assert(std::is_trivially_copyable_v<GameProjectSequencePlayOptions>);
static_assert(sizeof(GameProjectSequencePlayOptions) == kGameProjectSequencePlayOptionsV1Size);
static_assert(offsetof(GameProjectSequencePlayOptions, structSize) == 0u);
static_assert(offsetof(GameProjectSequencePlayOptions, version) == 4u);
static_assert(offsetof(GameProjectSequencePlayOptions, channel) == 24u);
static_assert(offsetof(GameProjectSequencePlayOptions, bindings) == 88u);
static_assert(offsetof(GameProjectSequencePlayOptions, bindingCount) == 96u);
static_assert(offsetof(GameProjectSequencePlayOptions, reserved) == 100u);
static_assert(std::is_standard_layout_v<GameProjectSequencePlaybackSnapshot>);
static_assert(std::is_trivially_copyable_v<GameProjectSequencePlaybackSnapshot>);
static_assert(sizeof(GameProjectSequencePlaybackSnapshot) == 136u);
static_assert(offsetof(GameProjectSequencePlaybackSnapshot, handle) == 0u);
static_assert(offsetof(GameProjectSequencePlaybackSnapshot, state) == 16u);
static_assert(offsetof(GameProjectSequencePlaybackSnapshot, timeSeconds) == 20u);
static_assert(offsetof(GameProjectSequencePlaybackSnapshot, assetGuid) == 32u);
static_assert(offsetof(GameProjectSequencePlaybackSnapshot, channel) == 65u);
static_assert(std::is_standard_layout_v<GameProjectSequenceEventClip>);
static_assert(std::is_trivially_copyable_v<GameProjectSequenceEventClip>);
static_assert(sizeof(GameProjectSequenceEventClip) == 72u);
static_assert(offsetof(GameProjectSequenceEventClip, startSeconds) == 64u);
static_assert(std::is_standard_layout_v<GameProjectSequenceTextClip>);
static_assert(std::is_trivially_copyable_v<GameProjectSequenceTextClip>);
static_assert(sizeof(GameProjectSequenceTextClip) == 192u);
static_assert(offsetof(GameProjectSequenceTextClip, tint) == 56u);
static_assert(offsetof(GameProjectSequenceTextClip, order) == 72u);
static_assert(offsetof(GameProjectSequenceTextClip, layout) == 80u);
static_assert(offsetof(GameProjectSequenceTextClip, tag) == 88u);
static_assert(offsetof(GameProjectSequenceTextClip, styleGuid) == 152u);
static_assert(std::is_standard_layout_v<GameProjectSequencePlaybackEvent>);
static_assert(std::is_trivially_copyable_v<GameProjectSequencePlaybackEvent>);
static_assert(sizeof(GameProjectSequencePlaybackEvent) == 328u);
static_assert(offsetof(GameProjectSequencePlaybackEvent, handle) == 0u);
static_assert(offsetof(GameProjectSequencePlaybackEvent, requestId) == 16u);
static_assert(offsetof(GameProjectSequencePlaybackEvent, kind) == 28u);
static_assert(offsetof(GameProjectSequencePlaybackEvent, assetGuid) == 32u);
static_assert(offsetof(GameProjectSequencePlaybackEvent, message) == 65u);

} // namespace HIKARI
