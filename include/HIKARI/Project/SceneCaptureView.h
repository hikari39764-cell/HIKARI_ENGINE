#pragma once

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/SceneCaptureTypes.h"

namespace HIKARI {

struct GameProjectSdkAccess;

// Retainable non-owning World capability. All calls are owner-thread only.
// Requests survive until Destroy or World teardown. Each frame renders the
// secondary views before the primary view, so bound screens show content of
// the same frame and never sample a target while it is being written. Up to
// kGameProjectSceneCaptureMaximumActiveCount independent captures may be
// active at once.
// Disabling or a transient render failure freezes the last good frame;
// Destroy retires the material binding and restores the authored material.
class SceneCaptureView {
public:
    SceneCaptureView() = default;
    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;

    HIKARI_GAME_PROJECT_SDK_API GameProjectSceneCaptureHandle
    Create(const GameProjectSceneCaptureDesc& desc) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Update(GameProjectSceneCaptureHandle capture,
                                            const GameProjectSceneCaptureDesc& desc) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Destroy(GameProjectSceneCaptureHandle capture) const noexcept;
    // 設定をコピーする。非有限値・範囲外・無効な handle は状態を変えず false を返す。
    // Update によるカメラ・寸法の変更はスタイルを保持する。
    HIKARI_GAME_PROJECT_SDK_API bool SetStyle(GameProjectSceneCaptureHandle capture,
                                              const GameProjectSceneCaptureStyleDesc& style) const noexcept;
    // 視点（最大 kGameProjectSceneCaptureMaximumViewCount）をコピーし、以後はカメラ物体の代わりにこれらで描く。count が 0 なら
    // カメラ物体の一枚に戻る。描き方（SetStyle）は全部の視点を描いた像全体に一度掛ける。矩形がキャプチャからはみ出す・
    // 値が有限でない・近い面と遠い面と投影の種類がそろわない時は状態を変えず false。Update で大きさを変えても視点は残り、
    // はみ出す視点は描かない。KeepPrevious の印の視点は前のフレームの絵を残し、描き方もその矩形には掛け直さない。
    HIKARI_GAME_PROJECT_SDK_API bool SetViews(GameProjectSceneCaptureHandle capture,
                                              const GameProjectSceneCaptureViewDesc* views, uint32_t count) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool BindMaterial(
        const GameProjectSceneCaptureMaterialBindingDesc& binding) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool BindMaterialRegion(
        const GameProjectSceneCaptureMaterialRegionBindingDesc& binding) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ClearMaterialBinding(GameProjectSceneCaptureHandle capture,
                                                          GameProjectObjectHandle modelObject) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool GetSnapshot(GameProjectSceneCaptureHandle capture,
                                                 GameProjectSceneCaptureSnapshot& outSnapshot) const noexcept;

private:
    friend struct GameProjectSdkAccess;
    explicit SceneCaptureView(GameProjectWorldHandle world) noexcept : world_(world) {}
    GameProjectWorldHandle world_{};
};

} // namespace HIKARI
