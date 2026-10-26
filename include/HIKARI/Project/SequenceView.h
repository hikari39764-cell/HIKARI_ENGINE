#pragma once

#include <cstdint>
#include <string_view>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/SequenceTypes.h"

namespace HIKARI {

struct GameProjectSdkAccess;

// Runtime sequence control is a non-owning view of the current World.
// Every operation resolves the generation-checked World identity again;
// retaining this view does not keep a World alive. Calls are valid only on
// the owning runtime thread while that World is current and not closing.
class SequenceView {
public:
    SequenceView() = default;

    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;

    // Play loads the sequence by its cooked/source Asset GUID and returns
    // a stable value+epoch handle. A zero handle means the request was
    // rejected. Play is synchronous on the owner thread; no worker queue
    // or hidden cross-thread handoff is performed.
    HIKARI_GAME_PROJECT_SDK_API GameProjectSequencePlaybackHandle
    Play(std::string_view assetGuid, const GameProjectSequencePlayOptions& options = {}) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Stop(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Pause(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Resume(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetPlaybackRate(GameProjectSequencePlaybackHandle handle,
                                                     float playbackRate) const noexcept;
    // この演出だけをゲーム停止から独立した時計へ切り替える（Editor Pause は維持）。
    // Pause/Resume と正負の倍速は共通。所有スレッド専用で、失効ハンドルやサービス不在は false。
    HIKARI_GAME_PROJECT_SDK_API bool SetUseUnscaledTime(GameProjectSequencePlaybackHandle handle,
                                                        bool enabled) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Seek(GameProjectSequencePlaybackHandle handle, float timeSeconds) const noexcept;

    HIKARI_GAME_PROJECT_SDK_API bool IsActive(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool IsPlaying(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool IsPaused(GameProjectSequencePlaybackHandle handle) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool GetSnapshot(GameProjectSequencePlaybackHandle handle,
                                                 GameProjectSequencePlaybackSnapshot& outSnapshot) const noexcept;
    // 流れている演出の事件段を out へ先頭から最大 capacity 件写し、全部の件数を返す（無効な handle は 0）。
    // 読み取りだけなので、Play の外でも編集器のプレビュー（GetEditorPreview の handle）に使える。
    HIKARI_GAME_PROJECT_SDK_API uint32_t GetEventClips(GameProjectSequencePlaybackHandle handle,
                                                       GameProjectSequenceEventClip* out,
                                                       uint32_t capacity) const noexcept;
    // 流れている演出の文字段を out へ先頭から最大 capacity 件写し、全部の件数を返す（無効な handle は 0）。
    // GetEventClips と同じく読み取りだけで、編集器のプレビューの handle にも使える。
    HIKARI_GAME_PROJECT_SDK_API uint32_t GetTextClips(GameProjectSequencePlaybackHandle handle,
                                                      GameProjectSequenceTextClip* out,
                                                      uint32_t capacity) const noexcept;
    // 文字段 clipId の文字を out へ終端付きで写し（UTF-8 の字の途中では切らない）、全体のバイト数を返す。
    // 片段が無ければ 0。capacity が足りなければ写せた所までで、戻り値で必要な大きさが分かる。
    HIKARI_GAME_PROJECT_SDK_API uint32_t GetTextClipText(GameProjectSequencePlaybackHandle handle, uint64_t clipId,
                                                         char* out, uint32_t capacity) const noexcept;
    // 演出ワークスペースのプレビュー（Play の外だけ）。編集中の演出の資産 GUID（保存前の変更も流れる）、
    // 時刻と状態を写す。プロジェクトは Play と同じ自分の効果をこの時刻で出せる。流れの操作はできない。
    HIKARI_GAME_PROJECT_SDK_API bool GetEditorPreview(GameProjectSequencePlaybackSnapshot& outSnapshot) const noexcept;

    // Consumes all events currently owned by this World. The callback and
    // its event reference are valid only during this call and execute on
    // the owning runtime thread. Null callbacks do not consume anything.
    // Completed and Rejected events are represented by the same fixed POD
    // contract as Started/Stopped/Pause/Resume/Seeked events.
    HIKARI_GAME_PROJECT_SDK_API uint32_t
    ConsumeEvents(void* context, GameProjectSequencePlaybackEventCallback callback) const noexcept;

private:
    friend struct GameProjectSdkAccess;

    explicit SequenceView(GameProjectWorldHandle world) noexcept : world_(world) {}

    GameProjectWorldHandle world_{};
};

} // namespace HIKARI
