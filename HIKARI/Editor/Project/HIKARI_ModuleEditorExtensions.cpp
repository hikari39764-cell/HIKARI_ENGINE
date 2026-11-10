#include "Editor/Project/HIKARI_ModuleEditorExtensions.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <numbers>

#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Project/HIKARI_GameProjectEditorModuleLibrary.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Platform/HIKARI_Win32Window.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {

GameProjectFloat3 ToFloat3(const MATH::Vec3& value) { return {value.x, value.y, value.z}; }

ImU32 ToColor(const EDITOR_PROJECT::EditorWorkspaceColor& color) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
}

// モジュールの呼び出しが投げても編集器は止めない。投げたら偽を返すので、呼び手が工具を外すか窓を閉じる。
template <class Call>
bool CallModule(const std::string& moduleId, const std::string& itemId, Call&& call) noexcept {
    try {
        call();
        return true;
    } catch (const std::exception& exception) {
        HIKARI_LOG_ERROR("[ProjectModules] " + moduleId + " " + itemId + " threw: " + exception.what());
    } catch (...) {
        HIKARI_LOG_ERROR("[ProjectModules] " + moduleId + " " + itemId + " threw an unknown exception.");
    }
    return false;
}

class ViewportCanvas final : public EDITOR_PROJECT::IEditorViewportCanvas {
public:
    ViewportCanvas(const Camera3D& camera, const SceneViewportRect& viewport, ImDrawList& drawList)
        : viewProjection_(camera.GetViewProj()), viewport_(viewport), drawList_(drawList) {}

    void DrawLine(const GameProjectFloat3& from, const GameProjectFloat3& to,
                  EDITOR_PROJECT::EditorWorkspaceColor color, float thickness) override {
        ImVec2 start{};
        ImVec2 end{};
        if (Project(from, start) && Project(to, end)) {
            drawList_.AddLine(start, end, ToColor(color), thickness);
        }
    }

    void DrawCircle(const GameProjectFloat3& center, float radius, EDITOR_PROJECT::EditorWorkspaceColor color,
                    float thickness) override {
        constexpr int kSegments = 64;
        GameProjectFloat3 previous{center.x + radius, center.y, center.z};
        for (int index = 1; index <= kSegments; ++index) {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(index) / kSegments;
            const GameProjectFloat3 next{center.x + std::cos(angle) * radius, center.y,
                                         center.z + std::sin(angle) * radius};
            DrawLine(previous, next, color, thickness);
            previous = next;
        }
    }

    void DrawHint(std::string_view utf8Text, EDITOR_PROJECT::EditorWorkspaceColor color) override {
        const ImVec2 position(viewport_.x + 14.0f, viewport_.y + 14.0f + 22.0f * static_cast<float>(hintLines_++));
        const ImVec2 size = ImGui::CalcTextSize(utf8Text.data(), utf8Text.data() + utf8Text.size());
        drawList_.AddRectFilled(ImVec2(position.x - 6.0f, position.y - 2.0f),
                                ImVec2(position.x + size.x + 6.0f, position.y + size.y + 2.0f), IM_COL32(0, 0, 0, 170),
                                3.0f);
        drawList_.AddText(position, ToColor(color), utf8Text.data(), utf8Text.data() + utf8Text.size());
    }

private:
    int hintLines_ = 0;
    bool Project(const GameProjectFloat3& point, ImVec2& outScreen) const {
        const MATH::Vec4 clip = viewProjection_.TransformPoint({point.x, point.y, point.z, 1.0f});
        if (clip.w <= 1.0e-4f) {
            return false;
        }
        const float inverseW = 1.0f / clip.w;
        outScreen.x = viewport_.x + (clip.x * inverseW * 0.5f + 0.5f) * viewport_.width;
        outScreen.y = viewport_.y + (1.0f - (clip.y * inverseW * 0.5f + 0.5f)) * viewport_.height;
        return true;
    }

    MATH::Mat4 viewProjection_{};
    SceneViewportRect viewport_{};
    ImDrawList& drawList_;
};

} // namespace

void ModuleEditorExtensions::Add(const GameProjectEditorModuleLibrary& module) {
    const std::string moduleId(module.GetModuleId());
    // id は操作と窓の見出しの鍵なので、項目全体で一意。後から来た同じ id は断る。
    const auto reject = [&moduleId](const char* kind, const std::string& id, const std::string& owner) {
        HIKARI_LOG_ERROR("[ProjectModules] " + moduleId + ": " + kind + " id " + id + " is already used by " + owner +
                         "; it was not added.");
    };
    for (size_t index = 0u; index < module.GetViewportToolCount(); ++index) {
        if (EDITOR_PROJECT::IEditorViewportTool* tool = module.GetViewportTool(index)) {
            const std::string id = tool->GetToolId();
            const auto existing =
                std::find_if(tools_.begin(), tools_.end(), [&](const Tool& entry) { return entry.id == id; });
            if (existing != tools_.end()) {
                reject("tool", id, existing->moduleId);
                continue;
            }
            tools_.push_back({moduleId, id, tool->GetDisplayName(), tool});
        }
    }
    for (size_t index = 0u; index < module.GetPanelCount(); ++index) {
        if (EDITOR_PROJECT::IEditorPanel* panel = module.GetPanel(index)) {
            const std::string id = panel->GetPanelId();
            const auto existing =
                std::find_if(panels_.begin(), panels_.end(), [&](const Panel& entry) { return entry.id == id; });
            if (existing != panels_.end()) {
                reject("window", id, existing->moduleId);
                continue;
            }
            panels_.push_back({moduleId, id, panel->GetDisplayName(), panel});
        }
    }
}

void ModuleEditorExtensions::RecordUndo(std::string_view label,
                                        std::unique_ptr<EDITOR_PROJECT::IEditorUndoEntry> entry) {
    if (undoRecorder_ && entry != nullptr) {
        undoRecorder_(std::string(label), std::move(entry));
    }
}

bool ModuleEditorExtensions::BakeFarModel(std::string_view modelAssetGuid, char* outFarModelGuid, size_t capacity) {
    const std::string guid = farModelBaker_ ? farModelBaker_(modelAssetGuid) : std::string{};
    if (guid.empty() || guid.size() >= capacity) {
        return false;
    }
    std::memcpy(outFarModelGuid, guid.c_str(), guid.size() + 1u);
    return true;
}

void ModuleEditorExtensions::SetSceneListener(EDITOR_PROJECT::IEditorSceneListener* listener) {
    if (listener != nullptr &&
        std::find(sceneListeners_.begin(), sceneListeners_.end(), listener) == sceneListeners_.end()) {
        sceneListeners_.push_back(listener);
    }
}

void ModuleEditorExtensions::NotifySceneSaved(std::string_view scenePathUtf8) {
    for (EDITOR_PROJECT::IEditorSceneListener* listener : sceneListeners_) {
        (void)CallModule("scene", "OnSceneSaved", [&] { listener->OnSceneSaved(scenePathUtf8); });
    }
}

void ModuleEditorExtensions::Clear() noexcept {
    tools_.clear();
    panels_.clear();
    sceneListeners_.clear();
    activeTool_ = static_cast<size_t>(-1);
}

const ModuleEditorExtensions::Tool* ModuleEditorExtensions::GetActiveTool() const noexcept {
    return activeTool_ < tools_.size() ? &tools_[activeTool_] : nullptr;
}

bool ModuleEditorExtensions::ActivateTool(std::string_view toolId) {
    const auto found = std::find_if(tools_.begin(), tools_.end(), [&](const Tool& tool) { return tool.id == toolId; });
    if (!toolId.empty() && found == tools_.end()) {
        return false;
    }
    if (activeTool_ < tools_.size()) {
        const Tool& previous = tools_[activeTool_];
        (void)CallModule(previous.moduleId, previous.id, [&] { previous.tool->OnDeactivate(); });
    }
    activeTool_ = static_cast<size_t>(-1);
    if (!toolId.empty() && CallModule(found->moduleId, found->id, [&] { found->tool->OnActivate(); })) {
        activeTool_ = static_cast<size_t>(found - tools_.begin());
    }
    return true;
}

bool ModuleEditorExtensions::SetPanelOpen(std::string_view panelId, bool open) {
    const auto found =
        std::find_if(panels_.begin(), panels_.end(), [&](const Panel& panel) { return panel.id == panelId; });
    if (found == panels_.end()) {
        return false;
    }
    found->open = open;
    return true;
}

bool ModuleEditorExtensions::UpdateViewport(const Camera3D& camera, const SceneViewportRect& viewport, bool hovered,
                                            float deltaSeconds, ImDrawList& drawList) {
    const GameProjectFloat3 cameraPosition = ToFloat3(camera.GetPosition());
    for (EDITOR_PROJECT::IEditorSceneListener* listener : sceneListeners_) {
        listener->OnViewportCamera(cameraPosition);
    }
    if (activeTool_ >= tools_.size()) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    EDITOR_PROJECT::EditorViewportInput input{};
    input.pointerInViewport = hovered;
    input.primaryPressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    input.primaryDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    input.primaryReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    input.shift = io.KeyShift;
    input.control = io.KeyCtrl;
    input.alt = io.KeyAlt;
    input.wheel = hovered ? io.MouseWheel : 0.0f;
    input.deltaSeconds = deltaSeconds;
    input.pressure = PLATFORM::CurrentPenPressure();
    input.typedCharacter =
        hovered && !io.WantTextInput && io.InputQueueCharacters.Size > 0 ? io.InputQueueCharacters[0] : 0u;
    const float normalizedX = (io.MousePos.x - viewport.x) / (std::max)(viewport.width, 1.0f);
    const float normalizedY = (io.MousePos.y - viewport.y) / (std::max)(viewport.height, 1.0f);
    EditorPointerRay ray{};
    if (BuildEditorPointerRay(camera, normalizedX, normalizedY, ray)) {
        input.rayOrigin = ToFloat3(ray.origin);
        input.rayDirection = ToFloat3(ray.direction);
        MATH::Vec3 ground{};
        if (IntersectEditorPlane(ray, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, ground)) {
            input.hasGroundPoint = true;
            input.groundPoint = ToFloat3(ground);
        }
    }
    ViewportCanvas canvas(camera, viewport, drawList);
    const Tool& active = tools_[activeTool_];
    if (!CallModule(active.moduleId, active.id, [&] { active.tool->Update(input, canvas); })) {
        (void)ActivateTool({});
    }
    return true;
}

void ModuleEditorExtensions::DrawPanels(EDITOR_PROJECT::IEditorWorkspaceUi& ui) {
    for (Panel& panel : panels_) {
        if (!panel.open) {
            continue;
        }
        const bool visible = ui.BeginPanel(panel.id, panel.name, &panel.open);
        if (visible && !CallModule(panel.moduleId, panel.id, [&] { panel.panel->Draw(ui); })) {
            panel.open = false;
        }
        ui.EndPanel();
    }
}

void ModuleEditorExtensions::DrawMenu() {
    if (tools_.empty() && panels_.empty()) {
        return;
    }
    if (!ImGui::BeginMenu(TEXT::Localize("menu.windows.modules").c_str())) {
        return;
    }
    for (Panel& panel : panels_) {
        ImGui::MenuItem(panel.name.c_str(), nullptr, &panel.open);
    }
    if (!tools_.empty() && !panels_.empty()) {
        ImGui::Separator();
    }
    for (size_t index = 0u; index < tools_.size(); ++index) {
        const bool active = activeTool_ == index;
        if (ImGui::MenuItem(tools_[index].name.c_str(), nullptr, active)) {
            (void)ActivateTool(active ? std::string_view{} : std::string_view(tools_[index].id));
        }
    }
    ImGui::EndMenu();
}

} // namespace HIKARI::EDITOR
