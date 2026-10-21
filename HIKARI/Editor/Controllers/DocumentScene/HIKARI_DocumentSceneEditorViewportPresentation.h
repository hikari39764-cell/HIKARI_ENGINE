#pragma once

#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/Viewport/HIKARI_SceneViewportSelectionService.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

#include <string>
#include <vector>

struct ImVec2;

namespace HIKARI {

class DocumentSceneBase;

namespace EDITOR {
namespace DOCUMENT_SCENE {

void DrawViewportDebugOptions(ViewportOverlayState& overlays, ViewportPerformanceState& performance);
bool DrawRenderDebugViewCombo(const char* label, ViewportDebugViewState& debugView, float width);
// 視口の画面上の印（網目の無い灯と機位）。選んでいない物は形（範囲・視錐台）を描かず、この小さな印だけで示す。
struct ViewportObjectIcon {
    enum class Kind { PointLight, SpotLight, Camera };
    SceneObjectId objectId{};
    Kind kind = Kind::PointLight;
    MATH::Vec2 screen{};
    MATH::Vec2 direction{};   // 画面上の向き（長さ 1。向きの無い灯は 0）
    unsigned int color = 0;   // ImU32
    float fade = 1.0f;        // 遠い印ほど薄く小さく（1 が最も近い）
    std::string label{};
};
// 灯（overlays.showLights）と機位（overlays.showCameras）の印を集める。点選に使うので選択の更新より先に呼ぶ。
std::vector<ViewportObjectIcon> CollectObjectOverlayIcons(DocumentSceneBase& scene, const ViewportOverlayState& overlays,
                                                          const ImVec2& viewportOrigin, const ImVec2& viewportSize);
std::vector<SceneViewportIconTarget> ObjectIconTargets(const std::vector<ViewportObjectIcon>& icons);
// 印を描く。名前は選んだ物とマウスが指している印だけ（overlays.showProbeLabels が切なら出さない）。
void DrawObjectOverlayIcons(const std::vector<ViewportObjectIcon>& icons, const ViewportOverlayState& overlays,
                            const std::vector<SceneObjectId>& selected, const ImVec2& mouse);

} // namespace DOCUMENT_SCENE
} // namespace EDITOR

} // namespace HIKARI
