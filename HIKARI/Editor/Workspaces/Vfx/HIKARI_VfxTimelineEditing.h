#pragma once

#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"
#include <optional>
#include <span>
#include <vector>

namespace HIKARI::EDITOR {
struct VfxTimelineCurve {
    VfxCurveAddress address;
    std::string label;
};
struct VfxTimelineClipboardKey {
    VfxCurveAddress address;
    VFX::VfxCurveKeyData key;
    float relativeSeconds = 0;
};
enum class VfxLayerTimeEdit { Move, TrimStart, TrimEnd, Retime };
struct VfxTimelineEditState {
    std::vector<std::string> selectedKeys;
    std::vector<VfxTimelineClipboardKey> clipboard;
    std::optional<VFX::VfxAssetData> baseline;
    std::vector<std::string> baselineSelection;
    std::string draggedLayer;
    VfxLayerTimeEdit layerMode = VfxLayerTimeEdit::Move;
    float dragMouseX = 0, dragAnchorSeconds = 0;
    bool keyDragging = false, marquee = false, duplicate = false, curveDragging = false;
    float curveMinimum = 0, curveMaximum = 1;
    float marqueeX = 0, marqueeY = 0;
    bool snap = true;
    bool autoKey = false;
    bool showMotionPath = true;
    bool suppressDragUntilRelease = false;
    static constexpr uint64_t mergeGroup = 0x56465854494d454full;
};

// 時間軸、AI、テストが同じキー操作を利用する。UI座標は保持しない。
std::vector<VfxTimelineCurve> CollectVfxTimelineCurves(const VFX::VfxEmitterData& layer, bool compositionOnly = false);
bool ResolveVfxLayerKeyTime(const VFX::VfxAssetData& asset, const VFX::VfxEmitterData& layer, float seconds,
                            float& normalized, std::string& error);
bool MoveVfxTimelineKeys(VFX::VfxAssetData& asset, std::span<const std::string> ids, float deltaSeconds,
                         std::string& error);
bool EditVfxLayerTime(VFX::VfxAssetData& asset, std::string_view layerId, VfxLayerTimeEdit mode, float deltaSeconds,
                      std::string& error);
std::vector<VfxTimelineClipboardKey> CopyVfxTimelineKeys(const VFX::VfxAssetData& asset,
                                                         std::span<const std::string> ids);
} // namespace HIKARI::EDITOR
