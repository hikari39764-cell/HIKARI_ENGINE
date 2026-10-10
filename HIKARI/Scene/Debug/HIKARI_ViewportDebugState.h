#pragma once

#include <optional>

#include "Render3D/Debug/HIKARI_RenderDebugView.h"

namespace HIKARI {

struct ViewportOverlayState {
    // デフォルトシーンには実体のグリッド床オブジェクトを置くため、
    // オーバーレイ地面は任意表示としてデフォルト無効。
    bool showGridFloor = false;
    bool showGrid = false;
    bool showAxis = true;
    bool showLights = true;
    bool showProbeLabels = true;
    bool showXRayGizmos = false;
    // 主ビューを成片と同じにする（グリッド・ギズモ・光源の印など編集の補助を一切描かない）。镜头演出の画面で使う。
    bool cleanFrame = false;
    // 明暗：画面を白黒にして、明暗の配置（値の構成）だけを見る。
    bool valueView = false;
    // 露出の固定：自動露出を止め、この EV100（補正前）の手動露出で描く。灯や主光の調整を自動露出が打ち消さない。
    // 編集中だけ効き、環境プロファイルには書かない。
    std::optional<float> lockedExposureEv100{};
    // 単独表示：この物体（場面の物体 ID、0 は無効）の灯以外の局所光を描かない。編集中だけ効き、文書は変えない。
    uint64_t soloLightObjectId = 0;
};

struct ViewportPerformanceState {
    bool disableSsaoInEditorViewport = false;
};

struct ViewportDebugViewState {
    RenderDebugView renderView = RenderDebugView::None;
    bool showLegend = true;
    bool freezeCullingCamera = false;
    bool showShadowCascades = false;
};

} // namespace HIKARI
