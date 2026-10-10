#include "Editor/Views/HIKARI_EditorViewGuides.h"

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>
#include <cmath>
#include <string>

#include <imgui.h>

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI::EDITOR {
namespace {

constexpr ImU32 kGuideColor = IM_COL32(255, 255, 255, 110);
constexpr ImU32 kGoldenColor = IM_COL32(255, 214, 120, 120);
constexpr ImU32 kSafeColor = IM_COL32(120, 210, 255, 120);
constexpr ImU32 kHorizonColor = IM_COL32(255, 150, 90, 170);
constexpr ImU32 kMaskColor = IM_COL32(0, 0, 0, 150);

float AspectRatio(ViewFrameAspect aspect) {
    switch (aspect) {
    case ViewFrameAspect::Widescreen: return 16.0f / 9.0f;
    case ViewFrameAspect::Cinemascope: return 2.39f;
    case ViewFrameAspect::Flat: return 1.85f;
    case ViewFrameAspect::Classic: return 4.0f / 3.0f;
    case ViewFrameAspect::Square: return 1.0f;
    case ViewFrameAspect::Vertical: return 9.0f / 16.0f;
    case ViewFrameAspect::Free:
    default: return 0.0f;
    }
}

// 水平方向（向きを持つ点の w=0）の消失点。カメラの前にあれば true。
bool ProjectDirection(const Camera3D& camera, const MATH::Vec3& direction, const ImVec2& origin, const ImVec2& size,
                      ImVec2& screen) {
    const MATH::Vec4 clip = camera.GetViewProj().TransformPoint({direction.x, direction.y, direction.z, 0.0f});
    if (clip.w <= 1.0e-5f) {
        return false;
    }
    screen = {origin.x + (clip.x / clip.w * 0.5f + 0.5f) * size.x,
              origin.y + (-clip.y / clip.w * 0.5f + 0.5f) * size.y};
    return true;
}

void DrawHorizon(ImDrawList& drawList, const Camera3D& camera, const ImVec2& origin, const ImVec2& size) {
    const MATH::Vec3 forward = camera.GetTarget() - camera.GetPosition();
    const float horizontal = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (horizontal <= 1.0e-4f) {
        return; // 真上か真下を向いている時は水平線が画面に無い。
    }
    const MATH::Vec3 ahead{forward.x / horizontal, 0.0f, forward.z / horizontal};
    const MATH::Vec3 side{-ahead.z, 0.0f, ahead.x};
    ImVec2 left{};
    ImVec2 right{};
    if (!ProjectDirection(camera, ahead - side * 0.5f, origin, size, left) ||
        !ProjectDirection(camera, ahead + side * 0.5f, origin, size, right) || std::abs(right.x - left.x) < 1.0e-3f) {
        return;
    }
    // 消失点を結ぶ線を視口の左右の端まで延ばす。
    const float slope = (right.y - left.y) / (right.x - left.x);
    const ImVec2 start{origin.x, left.y + (origin.x - left.x) * slope};
    const ImVec2 end{origin.x + size.x, left.y + (origin.x + size.x - left.x) * slope};
    drawList.AddLine(start, end, kHorizonColor, 1.5f);
}

void DrawFractionLines(ImDrawList& drawList, const ImVec2& min, const ImVec2& size, float fraction, ImU32 color) {
    for (const float t : {fraction, 1.0f - fraction}) {
        const float x = min.x + size.x * t;
        const float y = min.y + size.y * t;
        drawList.AddLine({x, min.y}, {x, min.y + size.y}, color);
        drawList.AddLine({min.x, y}, {min.x + size.x, y}, color);
    }
}

void DrawInsetFrame(ImDrawList& drawList, const ImVec2& min, const ImVec2& size, float scale, ImU32 color) {
    const ImVec2 inset{size.x * (1.0f - scale) * 0.5f, size.y * (1.0f - scale) * 0.5f};
    drawList.AddRect({min.x + inset.x, min.y + inset.y}, {min.x + size.x - inset.x, min.y + size.y - inset.y}, color);
}

} // namespace

void DrawEditorViewGuides(ImDrawList& drawList, float x, float y, float width, float height,
                          const EditorViewGuides& guides, const Camera3D* camera) {
    if (width <= 1.0f || height <= 1.0f) {
        return;
    }
    const ImVec2 viewMin{x, y};
    const ImVec2 viewMax{x + width, y + height};
    drawList.PushClipRect(viewMin, viewMax, true);

    // 画幅の枠。外側は暗くし、ほかの補助線は枠の中に描く。
    ImVec2 min = viewMin;
    ImVec2 size{width, height};
    if (const float ratio = AspectRatio(guides.aspect); ratio > 0.0f) {
        if (width / height > ratio) {
            size.x = height * ratio;
            min.x += (width - size.x) * 0.5f;
            drawList.AddRectFilled(viewMin, {min.x, viewMax.y}, kMaskColor);
            drawList.AddRectFilled({min.x + size.x, viewMin.y}, viewMax, kMaskColor);
        } else {
            size.y = width / ratio;
            min.y += (height - size.y) * 0.5f;
            drawList.AddRectFilled(viewMin, {viewMax.x, min.y}, kMaskColor);
            drawList.AddRectFilled({viewMin.x, min.y + size.y}, viewMax, kMaskColor);
        }
        drawList.AddRect(min, {min.x + size.x, min.y + size.y}, kGuideColor);
    }
    if (guides.thirds) {
        DrawFractionLines(drawList, min, size, 1.0f / 3.0f, kGuideColor);
    }
    if (guides.goldenRatio) {
        DrawFractionLines(drawList, min, size, 0.381966f, kGoldenColor);
    }
    if (guides.center) {
        const ImVec2 center{min.x + size.x * 0.5f, min.y + size.y * 0.5f};
        const float arm = (std::min)(size.x, size.y) * 0.03f;
        drawList.AddLine({center.x - arm, center.y}, {center.x + arm, center.y}, kGuideColor);
        drawList.AddLine({center.x, center.y - arm}, {center.x, center.y + arm}, kGuideColor);
    }
    if (guides.safeFrames) {
        DrawInsetFrame(drawList, min, size, 0.9f, kSafeColor);
        DrawInsetFrame(drawList, min, size, 0.8f, kSafeColor);
    }
    if (guides.horizon && camera != nullptr) {
        DrawHorizon(drawList, *camera, viewMin, {width, height});
    }
    drawList.PopClipRect();
}

bool DrawEditorViewGuidesMenu(EditorViewGuides& guides) {
    const auto label = [](const char* key) { return TEXT::Localize(key); };
    bool changed = false;
    changed |= ImGui::Checkbox(label("ui.viewGuides.thirds").c_str(), &guides.thirds);
    changed |= ImGui::Checkbox(label("ui.viewGuides.goldenRatio").c_str(), &guides.goldenRatio);
    changed |= ImGui::Checkbox(label("ui.viewGuides.center").c_str(), &guides.center);
    changed |= ImGui::Checkbox(label("ui.viewGuides.safeFrames").c_str(), &guides.safeFrames);
    changed |= ImGui::Checkbox(label("ui.viewGuides.horizon").c_str(), &guides.horizon);
    const auto aspectLabel = [&](ViewFrameAspect aspect) {
        return aspect == ViewFrameAspect::Free ? label("ui.viewGuides.aspectFree")
                                               : std::string(TEXT::EnumToName(kViewFrameAspectNames, aspect));
    };
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    if (ImGui::BeginCombo(label("ui.viewGuides.aspect").c_str(), aspectLabel(guides.aspect).c_str())) {
        for (const TEXT::EnumName<ViewFrameAspect>& entry : kViewFrameAspectNames) {
            if (ImGui::Selectable(aspectLabel(entry.value).c_str(), guides.aspect == entry.value)) {
                guides.aspect = entry.value;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void DrawEditorViewGuidesButton(const char* id, EditorViewGuides& guides) {
    ImGui::PushID(id);
    if (IconButton(EditorGlyph::Grid, "guides", EditorButtonTone::Quiet, {26, 26},
                   TEXT::Localize("ui.viewGuides.title").c_str())) {
        ImGui::OpenPopup("guidesPopup");
    }
    if (ImGui::BeginPopup("guidesPopup")) {
        ImGui::SeparatorText(TEXT::Localize("ui.viewGuides.title").c_str());
        (void)DrawEditorViewGuidesMenu(guides);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

} // namespace HIKARI::EDITOR

#endif
