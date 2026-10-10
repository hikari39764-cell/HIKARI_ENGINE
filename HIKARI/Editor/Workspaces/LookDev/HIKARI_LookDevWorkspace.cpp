#include "Editor/Workspaces/LookDev/HIKARI_LookDevWorkspace.h"

#if defined(HIKARI_WITH_EDITOR)

#include <cmath>
#include <string>

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_NumberField.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace HIKARI::EDITOR {
namespace {

constexpr LookDevPlaceItem kShapes[] = {
    LookDevPlaceItem::Box,    LookDevPlaceItem::Wall,    LookDevPlaceItem::Floor,
    LookDevPlaceItem::Pillar, LookDevPlaceItem::Ramp,    LookDevPlaceItem::Stairs,
    LookDevPlaceItem::Sphere, LookDevPlaceItem::Capsule, LookDevPlaceItem::Person,
};
constexpr LookDevPlaceItem kLights[] = {
    LookDevPlaceItem::PointLight,
    LookDevPlaceItem::SpotLight,
    LookDevPlaceItem::TubeLight,
    LookDevPlaceItem::RectLight,
};

std::string ItemLabel(LookDevPlaceItem item) {
    return TEXT::Localize(std::string("ui.lookDev.item.") + TEXT::EnumToName(kLookDevPlaceItemNames, item));
}

} // namespace

void LookDevWorkspace::DrawDockSpace(bool resetLayout) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID id = ImGui::GetID("HIKARI_LookDev_v2");
    if (resetLayout || ImGui::DockBuilderGetNode(id) == nullptr) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(id, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);
        ImGuiID center = id;
        ImGuiID left = 0;
        ImGuiID right = 0;
        ImGuiID lighting = 0;
        ImGuiID scene = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, &right, &center);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.62f, &lighting, &right);
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, &scene, &left);
        ImGui::DockBuilderDockWindow("###LookDev/Viewport", center);
        ImGui::DockBuilderDockWindow("###LookDev/Place", left);
        ImGui::DockBuilderDockWindow("###LookDev/Lights", scene);
        ImGui::DockBuilderDockWindow("###LookDev/Objects", scene);
        ImGui::DockBuilderDockWindow("###LookDev/Inspector", right);
        ImGui::DockBuilderDockWindow("###LookDev/Lighting", lighting);
        ImGui::DockBuilderFinish(id);
    }
    DrawWorkspaceDockSpace(id, viewport);
}

void LookDevWorkspace::DrawPalette() {
    const auto drawItems = [this](const char* titleKey, const auto& items) {
        ImGui::SeparatorText(TEXT::Localize(titleKey).c_str());
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
        int column = 0;
        for (const LookDevPlaceItem item : items) {
            if (column++ % 2 != 0) {
                ImGui::SameLine(0.0f, spacing);
            }
            const bool selected = armed_ == item;
            ImGui::PushID(static_cast<int>(item));
            if (ToggleButton(ItemLabel(item).c_str(), "##item", selected, ImVec2(width, 0.0f))) {
                armed_ = selected ? std::nullopt : std::optional<LookDevPlaceItem>(item);
            }
            ImGui::PopID();
        }
    };
    drawItems("ui.lookDev.shapes", kShapes);
    drawItems("ui.lookDev.lights", kLights);
    if (armed_) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lookDev.placeHint").c_str());
    }
    ImGui::Checkbox(TEXT::Localize("ui.lookDev.snap").c_str(), &snap_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!snap_);
    ImGui::SetNextItemWidth(-1.0f);
    (void)DragNumber("##gridStep", gridStep_, {.minimum = 0.05, .maximum = 10.0, .step = 0.05});
    ImGui::EndDisabled();
}

void LookDevWorkspace::HandlePlacementKeys(bool viewportFocused) {
    if (!armed_ || !viewportFocused || ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        armed_.reset();
    } else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        yawDegrees_ = std::fmod(yawDegrees_ + 90.0f, 360.0f);
    }
}

} // namespace HIKARI::EDITOR

#else

namespace HIKARI::EDITOR {
void LookDevWorkspace::DrawDockSpace(bool) {}
void LookDevWorkspace::DrawPalette() {}
void LookDevWorkspace::HandlePlacementKeys(bool) {}
} // namespace HIKARI::EDITOR

#endif
