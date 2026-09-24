#include "Assets/HIKARI_AssetDatabase.h"
#include "HIKARI/Project/FrameContext.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Ui/HIKARI_UiWorkspaceController.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <tuple>

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }
SceneObjectId CanvasOf(GameObject* object) {
    for (int depth = 0; object && depth < 64; object = object->GetParent(), ++depth)
        if (object->GetComponent<UiCanvasComponent>())
            return object->GetDocumentId();
    return {};
}
const SceneComponentData* RectData(const SceneDocument& document, SceneObjectId id) {
    for (const auto& o : document.objects)
        if (o.id == id && !o.editorLocked)
            for (const auto& c : o.components)
                if (c.type == "UiRectComponent")
                    return &c;
    return nullptr;
}
std::array<MATH::Vec2, 4> Corners(const UiResolvedLayout& r) {
    std::array<MATH::Vec2, 4> points{
        {{r.x, r.y}, {r.x + r.width, r.y}, {r.x + r.width, r.y + r.height}, {r.x, r.y + r.height}}};
    for (auto& p : points)
        r.transform.Transform(p.x, p.y);
    return points;
}
} // namespace
void UiWorkspaceController::DrawCanvas(DocumentSceneBase& scene, EditorContext& context, EditorCommandRouter& commands,
                                       EditorOperationRegistry& operations, UiWorkspaceResult& result) {
    const bool runtime = scene.IsRuntimePlayActive();
    if (!ImGui::Begin(Window("uix.canvas", "Canvas").c_str(), nullptr,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        FinishDrag(scene, nullptr);
        ClearGameViewportInputRect();
        return;
    }
    for (const auto [command, glyph, key] : {std::tuple{EditorCommandId::SaveDocument, EditorGlyph::Save, "uix.save"},
                                             std::tuple{EditorCommandId::Undo, EditorGlyph::Undo, "uix.undo"},
                                             std::tuple{EditorCommandId::Redo, EditorGlyph::Redo, "uix.redo"}}) {
        ImGui::BeginDisabled(!commands.CanExecute(command));
        if (IconButton(glyph, key, EditorButtonTone::Quiet, {28, 28}, L(key)))
            commands.Execute(command);
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (IconButton(EditorGlyph::Focus, "Fit", EditorButtonTone::Quiet, {28, 28}, L("uix.fit"))) {
        zoom_ = 1;
        pan_ = {};
    }
    ImGui::SameLine();
    if (IconToggleButton(EditorGlyph::Snap, "Snap", snap_, {28, 28}, L("uix.snap")))
        snap_ = !snap_;
    ImGui::SameLine();
    if (IconButton(runtime ? EditorGlyph::Stop : EditorGlyph::Play, "Play", EditorButtonTone::Quiet, {28, 28},
                   L(runtime ? "uix.stop" : "uix.play")))
        result.togglePlay = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(125);
    const char* devices[]{"1920 x 1080", "1280 x 720", "1080 x 1920", "2560 x 1080"};
    ImGui::Combo("##Device", &device_, devices, 4);
    ImGui::Separator();

    auto* selected = scene.GetWorld().FindObject(context.selection.GetActiveObjectId());
    if (const auto selectedCanvas = CanvasOf(selected); selectedCanvas.value)
        canvas_ = selectedCanvas;
    auto* canvasObject = scene.GetWorld().FindObject(canvas_);
    if (!canvasObject || !canvasObject->GetComponent<UiCanvasComponent>()) {
        canvas_ = {};
        canvasObject = nullptr;
        for (const auto& object : scene.GetWorld().GetObjects())
            if (object && object->GetComponent<UiCanvasComponent>()) {
                canvasObject = object.get();
                canvas_ = object->GetDocumentId();
                break;
            }
    }
    if (!canvasObject) {
        ImGui::TextWrapped("%s", L("uix.uiStart"));
        ImGui::End();
        ClearGameViewportInputRect();
        return;
    }
    const auto root = scene.GetAssetDatabase().GetProjectRoot();
    if (projectRoot_ != root) {
        projectRoot_ = root;
        content_.SetProjectRoot(root);
    }
    GAME_UI::GameUiFrameSource source;
    // 実行中はゲームと同じ全 Canvas / SDK 描画を使い、入力との食い違いを防ぐ。
    const auto* live = runtime ? scene.GetWorld().Services().Find<GAME_UI::GameUiPresentationService>() : nullptr;
    if (live) {
        source = live->GetFrameSource();
    } else {
        content_.SetAssetDatabase(&scene.GetAssetDatabase());
        content_.DiscardPending();
        FrameContext frame{};
        frame.frameIndex = ++serial_;
        projector_.BuildPresentation(scene.GetWorld(), frame, content_, canvas_);
        content_.Tick(serial_);
        source = content_.GetFrameSource();
    }
    auto* canvas = canvasObject->GetComponent<UiCanvasComponent>();
    if (!runtime && projector_.RejectedTextRequests())
        ImGui::TextWrapped("%s", L("uix.textSubmissionFailed"));
    float referenceWidth = canvas->GetReferenceWidth(), referenceHeight = canvas->GetReferenceHeight();
    const float aspect = device_ == 2 ? 9.f / 16 : device_ == 3 ? 2560.f / 1080 : 16.f / 9;
    const auto avail = ImGui::GetContentRegionAvail();
    const float height = std::max(1.f, std::min(avail.y, avail.x / aspect));
    const ImVec2 size{height * aspect * (runtime ? 1.f : zoom_), height * (runtime ? 1.f : zoom_)};
    const auto region = ImGui::GetCursorScreenPos();
    const ImVec2 origin{region.x + (avail.x - size.x) * .5f + (runtime ? 0 : pan_.x),
                        region.y + (avail.y - size.y) * .5f + (runtime ? 0 : pan_.y)};
    ImGui::InvisibleButton("UiCanvas", {std::max(1.f, avail.x), std::max(1.f, avail.y)},
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool focused = ImGui::IsWindowFocused();
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(region, {region.x + avail.x, region.y + avail.y}, true);
    const float scale = std::min(1.f, 2048.f / std::max(size.x, size.y));
    const auto width = static_cast<uint32_t>(std::max(size.x * scale, 64.f));
    const auto targetHeight = static_cast<uint32_t>(std::max(size.y * scale, 64.f));
    const auto image = target_.Render(source, width, targetHeight);
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(9, 13, 19, 255));
    if (image.ptr)
        draw->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(image.ptr)), origin,
                       {origin.x + size.x, origin.y + size.y});
    draw->AddRect(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(95, 112, 132, 190));
    if (!image.ptr || target_.FailedTextRequests()) {
        const char* message = L(!image.ptr ? "uix.previewFailed" : "uix.previewFontFailed");
        const float wrap = std::max(1.f, size.x - 16);
        const auto textSize = ImGui::CalcTextSize(message, nullptr, false, wrap);
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + textSize.y + 16}, IM_COL32(20, 24, 30, 235));
        draw->AddText(nullptr, 0, {origin.x + 8, origin.y + 8}, IM_COL32(255, 195, 100, 255), message, nullptr, wrap);
    }
    GameProjectUiCanvasFit fit{};
    ResolveGameProjectUiCanvasFit(static_cast<uint32_t>(std::max(1.f, size.x)),
                                  static_cast<uint32_t>(std::max(1.f, size.y)), referenceWidth, referenceHeight, fit);
    const auto toScreen = [&](MATH::Vec2 p) {
        return ImVec2{origin.x + fit.offsetX + p.x * fit.scale, origin.y + fit.offsetY + p.y * fit.scale};
    };
    float mouseX = 0, mouseY = 0;
    ProjectGameUiScreenPointToCanvas(fit, ImGui::GetIO().MousePos.x - origin.x, ImGui::GetIO().MousePos.y - origin.y,
                                     mouseX, mouseY);
    if (!runtime) {
        UiResolvedLayout selectedLayout;
        bool resize = false;
        if (selected && selected->GetComponent<UiRectComponent>()) {
            selectedLayout = projector_.ResolveLayout(scene.GetWorld(), *selected);
            if (selectedLayout.valid && selectedLayout.canvas == canvas_) {
                const auto corners = Corners(selectedLayout);
                for (int i = 0; i < 4; ++i)
                    draw->AddLine(toScreen(corners[i]), toScreen(corners[(i + 1) % 4]), IM_COL32(50, 210, 221, 255), 2);
                const auto handle = toScreen(corners[2]);
                draw->AddRectFilled({handle.x - 4, handle.y - 4}, {handle.x + 4, handle.y + 4},
                                    IM_COL32(70, 225, 235, 255));
                resize = std::abs(ImGui::GetIO().MousePos.x - handle.x) < 9 &&
                         std::abs(ImGui::GetIO().MousePos.y - handle.y) < 9;
            }
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            GameObject* picked = resize ? selected : nullptr;
            int best = INT_MIN;
            if (!picked)
                for (const auto& object : scene.GetWorld().GetObjects()) {
                    if (!object || !object->GetComponent<UiRectComponent>())
                        continue;
                    const auto layout = projector_.ResolveLayout(scene.GetWorld(), *object);
                    if (layout.canvas == canvas_ && layout.order >= best && ContainsUiPoint(layout, mouseX, mouseY)) {
                        picked = object.get();
                        best = layout.order;
                    }
                }
            if (picked) {
                context.selection.SelectObjectIds(scene.GetWorld(), {picked->GetDocumentId()});
                const auto* parent = picked->GetParent();
                const auto* group = parent ? parent->GetComponent<UiLayoutGroupComponent>() : nullptr;
                if (!animation_.IsPreviewing() && (!group || !group->IsEnabled()))
                    if (const auto* data = RectData(scene.GetSceneDocument(), picked->GetDocumentId())) {
                        float angle = 0;
                        if (parent && parent->GetComponent<UiRectComponent>())
                            angle = projector_.ResolveLayout(scene.GetWorld(), *picked->GetParent()).rotationRadians;
                        if (resize)
                            angle = projector_.ResolveLayout(scene.GetWorld(), *picked).rotationRadians;
                        drag_ = Drag{picked->GetDocumentId(),
                                     *data,
                                     data->properties,
                                     {mouseX, mouseY},
                                     {std::cos(angle), std::sin(angle)},
                                     scene.GetSceneDocumentRevision(),
                                     resize};
                        const auto initial = projector_.ResolveLayout(scene.GetWorld(), *picked);
                        drag_->size = {initial.width, initial.height};
                    }
            }
        }
        if (drag_) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || drag_->revision != scene.GetSceneDocumentRevision())
                FinishDrag(scene, nullptr);
            else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                FinishDrag(scene, &operations);
            else {
                const float x = mouseX - drag_->mouse.x, y = mouseY - drag_->mouse.y;
                float dx = x * drag_->parentDelta.x + y * drag_->parentDelta.y;
                float dy = -x * drag_->parentDelta.y + y * drag_->parentDelta.x;
                if (snap_) {
                    dx = std::round(dx / 8) * 8;
                    dy = std::round(dy / 8) * 8;
                }
                auto lo = drag_->before.properties.value("offsetMinimum", std::array<float, 2>{-160, -40});
                auto hi = drag_->before.properties.value("offsetMaximum", std::array<float, 2>{160, 40});
                if (!drag_->resize) {
                    lo[0] += dx;
                    lo[1] += dy;
                } else {
                    dx = std::max(dx, 4.f - drag_->size.x);
                    dy = std::max(dy, 4.f - drag_->size.y);
                    const auto pivot = drag_->before.properties.value("pivot", std::array<float, 2>{.5f, .5f});
                    const float angle = drag_->before.properties.value("rotationDegrees", 0.f) * .01745329251994329577f;
                    const float pivotDeltaX = dx * pivot[0], pivotDeltaY = dy * pivot[1];
                    const float shiftX = pivotDeltaX - (pivotDeltaX * std::cos(angle) - pivotDeltaY * std::sin(angle));
                    const float shiftY = pivotDeltaY - (pivotDeltaX * std::sin(angle) + pivotDeltaY * std::cos(angle));
                    lo[0] += shiftX;
                    hi[0] += shiftX;
                    lo[1] += shiftY;
                    hi[1] += shiftY;
                }
                drag_->changed = dx != 0 || dy != 0;
                hi[0] += dx;
                hi[1] += dy;
                drag_->after["offsetMinimum"] = lo;
                drag_->after["offsetMaximum"] = hi;
                if (auto* object = scene.GetWorld().FindObject(drag_->object))
                    if (auto* rect = object->GetComponent<UiRectComponent>())
                        rect->Deserialize(drag_->after);
            }
        }
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            pan_.x += ImGui::GetIO().MouseDelta.x;
            pan_.y += ImGui::GetIO().MouseDelta.y;
        }
        if (hovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0)
            zoom_ = std::clamp(zoom_ * std::pow(1.15f, ImGui::GetIO().MouseWheel), .25f, 4.f);
    }
    draw->PopClipRect();
    if (runtime) {
        SERVICES::SetEditorGameViewportSize(static_cast<int>(size.x), static_cast<int>(size.y), true);
        SetGameViewportInputRect(origin.x, origin.y, size.x, size.y, focused);
        SetGameViewportGizmoCapture(false);
        result.playViewport = {static_cast<int32_t>(origin.x), static_cast<int32_t>(origin.y),
                               static_cast<int32_t>(origin.x + size.x), static_cast<int32_t>(origin.y + size.y)};
        result.playViewportClicked = hovered &&
                                     ImGui::IsMouseHoveringRect(origin, {origin.x + size.x, origin.y + size.y}) &&
                                     ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    } else
        ClearGameViewportInputRect();
    if (!runtime && !drag_)
        commands.ProcessViewportShortcuts(focused && hovered);
    ImGui::End();
}
} // namespace HIKARI::EDITOR
