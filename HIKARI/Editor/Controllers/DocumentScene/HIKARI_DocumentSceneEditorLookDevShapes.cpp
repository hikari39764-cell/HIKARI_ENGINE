#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevDocument.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevShapeHandles.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
namespace {

constexpr ImU32 kShapeColor = IM_COL32(120, 230, 255, 220);
constexpr ImU32 kCutColor = IM_COL32(255, 170, 90, 230);
constexpr ImU32 kPreviewColor = IM_COL32(255, 238, 150, 235);

void DrawSizeLabel(ImDrawList& drawList, const ImVec2& at, const std::string& text) {
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const ImVec2 origin{at.x + 14.0f, at.y + 10.0f};
    drawList.AddRectFilled(origin, {origin.x + size.x + 12.0f, origin.y + size.y + 8.0f}, IM_COL32(8, 12, 17, 215),
                           4.0f);
    drawList.AddText({origin.x + 6.0f, origin.y + 4.0f}, IM_COL32(220, 240, 255, 250), text.c_str());
}

void DrawSquare(ImDrawList& drawList, const MATH::Vec2& at, bool hot, ImU32 color) {
    const float half = hot ? 6.0f : 4.5f;
    drawList.AddRectFilled({at.x - half, at.y - half}, {at.x + half, at.y + half},
                           hot ? IM_COL32(240, 250, 255, 255) : color, 1.5f);
    drawList.AddRect({at.x - half, at.y - half}, {at.x + half, at.y + half}, IM_COL32(8, 24, 32, 230), 1.5f, 0, 1.2f);
}

// 開口の線（四角は箱、丸は円柱）。
void DrawCut(ImDrawList& drawList, const Camera3D& camera, const EDITOR::LookDevView& view,
             const EDITOR::LookDevBoxFrame& frame, const ProceduralMeshCut& cut, ImU32 color) {
    if (cut.roundAxis >= 0) {
        EDITOR::DrawLookDevCylinder(drawList, camera, view, frame, cut.roundAxis,
                                    EDITOR::LookDevAxis(cut.size, (cut.roundAxis + 1) % 3) * 0.5f,
                                    EDITOR::LookDevAxis(cut.size, cut.roundAxis), color);
    } else {
        EDITOR::DrawLookDevBox(drawList, camera, view, frame, cut.size, {}, color);
    }
}

ProceduralMeshSettings ReadShape(const SceneComponentData& component) {
    ProceduralMeshComponent probe;
    probe.Deserialize(component.properties);
    return probe.GetSettings();
}

void WriteShape(SceneComponentData& component, const ProceduralMeshSettings& settings) {
    component.properties["width"] = settings.width;
    component.properties["height"] = settings.height;
    component.properties["depth"] = settings.depth;
    component.properties["cuts"] = WriteProceduralMeshCuts(settings.cuts);
}

} // namespace

bool DocumentSceneEditorController::UpdateLookDevShapeHandles(DocumentSceneBase& scene,
                                                              const EDITOR::SceneViewportRect& viewport, bool hovered,
                                                              bool gizmoCapture) {
    const SceneObjectId selected = context_.selection.GetActiveObjectId();
    GameObject* object = context_.selection.GetSelectedObjectCount() == 1u && selected.value != 0u
                             ? scene.GetWorld().FindObject(selected)
                             : nullptr;
    const ProceduralMeshComponent* shape =
        object != nullptr ? object->GetComponent<ProceduralMeshComponent>() : nullptr;
    if (shape == nullptr || EDITOR::IsObjectEditorLocked(scene.GetSceneDocument(), selected)) {
        lookDevShapeDrag_.reset();
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const Camera3D& camera = scene.GetCamera();
    const EDITOR::LookDevView view{viewport.x, viewport.y, viewport.width, viewport.height};
    const MATH::Mat4 world = object->GetTransform().GetWorldMatrix();
    const ProceduralMeshSettings& settings = shape->GetSettings();
    const bool flat = settings.kind == ProceduralMeshKind::Plane || settings.kind == ProceduralMeshKind::GridPlane;
    const auto ray = [&] {
        EDITOR::EditorPointerRay result{};
        const bool valid = EDITOR::BuildEditorPointerRay(camera, (io.MousePos.x - viewport.x) / viewport.width,
                                                         (io.MousePos.y - viewport.y) / viewport.height, result);
        return valid ? std::optional<EDITOR::EditorPointerRay>(result) : std::nullopt;
    };

    // 切り抜き（開口）の線はいつも出す。
    for (const ProceduralMeshCut& cut : settings.cuts) {
        DrawCut(drawList, camera, view, EDITOR::LookDevFrameFromWorld(world, cut.center), cut, kCutColor);
    }

    if (lookDevShapeDrag_ && lookDevShapeDrag_->objectId == selected) {
        LookDevShapeDrag& drag = *lookDevShapeDrag_;
        const bool cut = drag.cutIndex >= 0 && static_cast<size_t>(drag.cutIndex) < drag.start.cuts.size();
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (const auto pointer = ray()) {
                drag.result = drag.start;
                drag.worldShift = {};
                if (!cut) {
                    // 箱の面：寸法を変え、片側なら中心を動かす。切り抜きは世界に留まるよう局所の中心を戻す。
                    const EDITOR::LookDevBoxFrame frame = EDITOR::LookDevFrameFromWorld(drag.startWorld);
                    const int axis = drag.handle.axis;
                    if (const auto resize = EDITOR::ResizeLookDevBox(
                            frame, EDITOR::LookDevAxis(GetProceduralMeshExtent(drag.start), axis), drag.handle,
                            *pointer, lookDevWorkspace_.GridStep())) {
                        drag.result = EDITOR::WithLookDevShapeExtent(drag.start, axis, resize->extent);
                        for (ProceduralMeshCut& moved : drag.result.cuts) {
                            EDITOR::LookDevAxis(moved.center, axis) -= resize->localShift;
                        }
                        drag.result = SanitizeProceduralMeshSettings(drag.result);
                        drag.worldShift = resize->worldShift;
                    }
                } else if (drag.moveCut) {
                    // 開口を薄い軸に垂直な面の上で動かす。
                    ProceduralMeshCut& moved = drag.result.cuts[static_cast<size_t>(drag.cutIndex)];
                    const int thin = EDITOR::LookDevThinAxis(drag.start, object->GetTransform().scale);
                    const EDITOR::LookDevBoxFrame frame = EDITOR::LookDevFrameFromWorld(drag.startWorld, moved.center);
                    const MATH::Vec3 normal = MATH::Normalize(frame.axes[thin]);
                    MATH::Vec3 hit{};
                    if (EDITOR::IntersectEditorPlane(*pointer, frame.center, normal, hit)) {
                        const MATH::Vec3 delta = hit - frame.center;
                        const float grid = lookDevWorkspace_.GridStep();
                        for (int axis = 0; axis < 3; ++axis) {
                            const MATH::Vec3& row = frame.axes[axis];
                            const float scale = MATH::Length(row);
                            if (axis == thin || !(scale > 1.0e-6f)) {
                                continue;
                            }
                            float along = MATH::Dot(delta, row) / scale;
                            along = grid > 0.0f ? std::round(along / grid) * grid : along;
                            EDITOR::LookDevAxis(moved.center, axis) += along / scale;
                        }
                    }
                } else {
                    ProceduralMeshCut& resized = drag.result.cuts[static_cast<size_t>(drag.cutIndex)];
                    const EDITOR::LookDevBoxFrame frame =
                        EDITOR::LookDevFrameFromWorld(drag.startWorld, resized.center);
                    if (const auto resize =
                            EDITOR::ResizeLookDevBox(frame, EDITOR::LookDevAxis(resized.size, drag.handle.axis),
                                                     drag.handle, *pointer, lookDevWorkspace_.GridStep())) {
                        if (resized.roundAxis >= 0) {
                            // 丸は直径を二軸で揃える。
                            EDITOR::LookDevAxis(resized.size, (resized.roundAxis + 1) % 3) = resize->extent;
                            EDITOR::LookDevAxis(resized.size, (resized.roundAxis + 2) % 3) = resize->extent;
                        } else {
                            EDITOR::LookDevAxis(resized.size, drag.handle.axis) = resize->extent;
                            EDITOR::LookDevAxis(resized.center, drag.handle.axis) += resize->localShift;
                        }
                        drag.result = SanitizeProceduralMeshSettings(drag.result);
                    }
                }
            }
            // 離すまでは線だけを描き、離した時に一度だけ形を作り直す（形ごとに幾何を焼くため）。
            if (cut) {
                const ProceduralMeshCut& preview = drag.result.cuts[static_cast<size_t>(drag.cutIndex)];
                const EDITOR::LookDevBoxFrame frame = EDITOR::LookDevFrameFromWorld(drag.startWorld, preview.center);
                DrawCut(drawList, camera, view, frame, preview, kPreviewColor);
                DrawSizeLabel(drawList, io.MousePos, EDITOR::FormatLookDevSize(frame, preview.size, false));
            } else {
                const EDITOR::LookDevBoxFrame frame = EDITOR::LookDevFrameFromWorld(drag.startWorld);
                const MATH::Vec3 extent = GetProceduralMeshExtent(drag.result);
                EDITOR::DrawLookDevBox(drawList, camera, view, frame, extent, drag.worldShift, kPreviewColor);
                DrawSizeLabel(drawList, io.MousePos, EDITOR::FormatLookDevSize(frame, extent, flat));
            }
            return true;
        }
        const LookDevShapeDrag finished = std::move(drag);
        lookDevShapeDrag_.reset();
        if (cut && !(finished.result == finished.start) &&
            static_cast<size_t>(finished.cutIndex) < finished.result.cuts.size()) {
            // 開口は procedural.opening で変える（智能体と同じ検査と取り消し）。
            const ProceduralMeshCut& edited = finished.result.cuts[static_cast<size_t>(finished.cutIndex)];
            RunLookDevOperation("procedural.opening", {{"action", "edit"},
                                                       {"objectId", selected.value},
                                                       {"index", finished.cutIndex},
                                                       {"center", {edited.center.x, edited.center.y, edited.center.z}},
                                                       {"size", {edited.size.x, edited.size.y, edited.size.z}}});
        } else if (!cut && !(finished.result == finished.start)) {
            ContinueLookDevObjectDrag(scene, selected, "Resize Shape", [&](SceneObjectData& document) {
                SceneComponentData* component = EDITOR::FindLookDevComponent(document, "ProceduralMeshComponent");
                if (component == nullptr) {
                    return false;
                }
                WriteShape(*component, finished.result);
                document.transform.position = document.transform.position +
                                              EDITOR::LookDevParentLocalVector(scene, document, finished.worldShift);
                return true;
            });
            FinishLookDevObjectDrag(scene);
        }
        return true;
    }
    lookDevShapeDrag_.reset();

    // 手柄：形の面（水色）、開口の面（橙）、開口の中心（丸、動かす）。
    struct Candidate {
        int cutIndex = -1;
        bool moveCut = false;
        EDITOR::LookDevShapeHandle handle{};
    };
    std::vector<Candidate> candidates;
    for (const EDITOR::LookDevShapeHandle& handle : EDITOR::BuildLookDevShapeHandles(settings, world)) {
        candidates.push_back({-1, false, handle});
    }
    for (size_t index = 0; index < settings.cuts.size(); ++index) {
        const ProceduralMeshCut& cut = settings.cuts[index];
        const EDITOR::LookDevBoxFrame frame = EDITOR::LookDevFrameFromWorld(world, cut.center);
        const int thin =
            cut.roundAxis >= 0 ? cut.roundAxis : EDITOR::LookDevThinAxis(settings, object->GetTransform().scale);
        const uint8_t mask = static_cast<uint8_t>(0b111u & ~(1u << thin));
        // 丸は直径の手柄（対称）を一つずつ、四角は面ごとに片側。
        for (const EDITOR::LookDevShapeHandle& handle :
             EDITOR::BuildLookDevBoxHandles(frame, cut.size, mask, cut.roundAxis >= 0 ? mask : uint8_t{0})) {
            candidates.push_back({static_cast<int>(index), false, handle});
        }
        candidates.push_back({static_cast<int>(index), true, {0, 1.0f, false, frame.center}});
    }
    std::optional<size_t> hot{};
    if (hovered && !gizmoCapture) {
        float nearest = 9.0f;
        for (size_t index = 0; index < candidates.size(); ++index) {
            MATH::Vec2 screen{};
            if (EDITOR::ProjectLookDevPoint(camera, view, candidates[index].handle.position, screen)) {
                const float distance = std::hypot(screen.x - io.MousePos.x, screen.y - io.MousePos.y);
                if (distance < nearest) {
                    nearest = distance;
                    hot = index;
                }
            }
        }
    }
    drawList.PushClipRect({viewport.x, viewport.y}, {viewport.x + viewport.width, viewport.y + viewport.height}, true);
    for (size_t index = 0; index < candidates.size(); ++index) {
        const Candidate& candidate = candidates[index];
        MATH::Vec2 screen{};
        if (!EDITOR::ProjectLookDevPoint(camera, view, candidate.handle.position, screen)) {
            continue;
        }
        if (candidate.moveCut) {
            drawList.AddCircleFilled({screen.x, screen.y}, hot == index ? 7.0f : 5.5f,
                                     hot == index ? IM_COL32(255, 244, 220, 255) : kCutColor);
            drawList.AddCircle({screen.x, screen.y}, hot == index ? 7.0f : 5.5f, IM_COL32(40, 20, 8, 230), 0, 1.2f);
        } else {
            DrawSquare(drawList, screen, hot == index, candidate.cutIndex >= 0 ? kCutColor : kShapeColor);
        }
    }
    drawList.PopClipRect();
    if (!hot) {
        return false;
    }
    const Candidate& chosen = candidates[*hot];
    if (chosen.cutIndex >= 0) {
        const ProceduralMeshCut& cut = settings.cuts[static_cast<size_t>(chosen.cutIndex)];
        DrawSizeLabel(drawList, io.MousePos,
                      EDITOR::FormatLookDevSize(EDITOR::LookDevFrameFromWorld(world, cut.center), cut.size, false));
    } else {
        DrawSizeLabel(
            drawList, io.MousePos,
            EDITOR::FormatLookDevSize(EDITOR::LookDevFrameFromWorld(world), GetProceduralMeshExtent(settings), flat));
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        lookDevShapeDrag_ =
            LookDevShapeDrag{selected, chosen.handle, settings, world, settings, {}, chosen.cutIndex, chosen.moveCut};
    }
    return true;
}

EDITOR::EditorOperationResult DocumentSceneEditorController::EditLookDevOpening(const nlohmann::json& p) {
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr || scene->IsRuntimePlayActive()) {
        return EDITOR::OperationFailure("EDIT_MODE_REQUIRED", "Edit openings outside Play mode.");
    }
    const std::string action = p.at("action").get<std::string>();
    if (action != "add" && action != "edit" && action != "remove") {
        return EDITOR::OperationFailure("INVALID_ACTION", "action is add, edit or remove.");
    }
    const SceneObjectId objectId{p.at("objectId").get<uint64_t>()};
    EDITOR::EditorOperationResult failure{};
    const auto fail = [&](std::string_view code, std::string message) {
        failure = EDITOR::OperationFailure(code, std::move(message));
        return false;
    };
    nlohmann::json data{{"objectId", objectId.value}};
    const char* label = action == "add" ? "Add Opening" : (action == "edit" ? "Edit Opening" : "Remove Opening");
    const bool done = EditLookDevDocument(*scene, label, [&](SceneDocument& document) {
        SceneObjectData* object = FindSceneObjectData(document, objectId);
        SceneComponentData* component =
            object != nullptr ? EDITOR::FindLookDevComponent(*object, "ProceduralMeshComponent") : nullptr;
        if (component == nullptr) {
            return fail("NOT_A_SHAPE", "The object has no ProceduralMeshComponent.");
        }
        if (EDITOR::IsObjectEditorLocked(document, objectId)) {
            return fail("OBJECT_LOCKED", "The object is locked in the editor.");
        }
        ProceduralMeshSettings settings = ReadShape(*component);
        if (settings.kind != ProceduralMeshKind::Box) {
            return fail("NOT_A_BOX", "Openings can only be cut into Box shapes.");
        }
        size_t index = 0;
        if (action == "add") {
            if (settings.cuts.size() >= kMaximumProceduralMeshCuts) {
                return fail("TOO_MANY_OPENINGS",
                            "A shape holds at most " + std::to_string(kMaximumProceduralMeshCuts) + " openings.");
            }
            const std::optional<EDITOR::LookDevOpeningKind> kind =
                TEXT::TryParseEnum(EDITOR::kLookDevOpeningKindNames, p.value("kind", std::string{}));
            if (!kind) {
                return fail("INVALID_KIND", "kind is hole, round_hole, door, window or round_window.");
            }
            index = settings.cuts.size();
            settings.cuts.push_back(EDITOR::BuildLookDevOpening(settings, object->transform.scale, *kind));
        } else {
            const int64_t requested = p.value("index", int64_t{-1});
            if (requested < 0 || static_cast<size_t>(requested) >= settings.cuts.size()) {
                return fail("INVALID_INDEX", "index must name an existing opening (0 to " +
                                                 std::to_string(settings.cuts.size()) + " - 1).");
            }
            index = static_cast<size_t>(requested);
            if (action == "remove") {
                settings.cuts.erase(settings.cuts.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
        if (action != "remove") {
            // 位置と大きさは物体の局所（箱の中心が原点、m ではなく局所の長さ）。
            ProceduralMeshCut& cut = settings.cuts[index];
            cut.center = JSONREAD::Vec3MemberOr(p, "center", cut.center);
            cut.size = JSONREAD::Vec3MemberOr(p, "size", cut.size);
            if (!MATH::IsFinite(cut.center) || !MATH::IsFinite(cut.size)) {
                return fail("INVALID_OPENING", "center and size must be finite.");
            }
        }
        const size_t expected = settings.cuts.size();
        settings = SanitizeProceduralMeshSettings(settings);
        if (settings.cuts.size() != expected) {
            return fail("OPENING_REJECTED", "The opening overlaps another opening or lies outside the box.");
        }
        WriteShape(*component, settings);
        if (action != "remove") {
            const ProceduralMeshCut& stored = settings.cuts[index];
            data["index"] = index;
            data["opening"] = {{"center", {stored.center.x, stored.center.y, stored.center.z}},
                               {"size", {stored.size.x, stored.size.y, stored.size.z}},
                               {"round", stored.roundAxis >= 0}};
        }
        data["openingCount"] = settings.cuts.size();
        return true;
    });
    return done ? EDITOR::OperationOk(action == "add" ? "Opening added."
                                                      : (action == "edit" ? "Opening updated." : "Opening removed."),
                                      std::move(data))
                : failure;
}

EDITOR::EditorOperationResult DocumentSceneEditorController::SubtractLookDevShapes(const nlohmann::json& p) {
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr || scene->IsRuntimePlayActive()) {
        return EDITOR::OperationFailure("EDIT_MODE_REQUIRED", "Subtract shapes outside Play mode.");
    }
    const SceneObjectId cutterId{p.at("cutterId").get<uint64_t>()};
    const GameObject* cutterObject = scene->GetWorld().FindObject(cutterId);
    const ProceduralMeshComponent* cutterShape =
        cutterObject != nullptr ? cutterObject->GetComponent<ProceduralMeshComponent>() : nullptr;
    if (cutterShape == nullptr) {
        return EDITOR::OperationFailure("NOT_A_SHAPE", "The cutter must be a procedural shape.");
    }
    std::unordered_set<uint64_t> targets;
    for (const nlohmann::json& id : p.value("targetIds", nlohmann::json::array())) {
        targets.insert(id.get<uint64_t>());
    }
    const bool keepCutter = p.value("keepCutter", false);
    // 挖空方块の 8 つの角（世界）。
    const MATH::Mat4 cutterWorld = cutterObject->GetTransform().GetWorldMatrix();
    const MATH::Vec3 half = GetProceduralMeshExtent(cutterShape->GetSettings()) * 0.5f;
    std::array<MATH::Vec3, 8> corners{};
    for (int index = 0; index < 8; ++index) {
        const MATH::Vec4 corner = cutterWorld.TransformPoint(
            {(index & 1) ? half.x : -half.x, (index & 2) ? half.y : -half.y, (index & 4) ? half.z : -half.z, 1.0f});
        corners[static_cast<size_t>(index)] = {corner.x, corner.y, corner.z};
    }
    nlohmann::json changed = nlohmann::json::array();
    const bool done = EditLookDevDocument(*scene, "Cut Out", [&](SceneDocument& document) {
        const std::unordered_set<uint64_t> cutterTree = CollectObjectSubtreeIds(document, cutterId);
        for (SceneObjectData& target : document.objects) {
            SceneComponentData* component = EDITOR::FindLookDevComponent(target, "ProceduralMeshComponent");
            const GameObject* runtime = scene->GetWorld().FindObject(target.id);
            if (component == nullptr || runtime == nullptr || cutterTree.contains(target.id.value) ||
                (!targets.empty() && !targets.contains(target.id.value)) ||
                EDITOR::IsObjectEditorLocked(document, target.id)) {
                continue;
            }
            ProceduralMeshSettings settings = ReadShape(*component);
            if (settings.kind != ProceduralMeshKind::Box || settings.cuts.size() >= kMaximumProceduralMeshCuts) {
                continue;
            }
            // 角を相手の局所へ移して外接箱を取る（向きが揃っていれば正確、斜めなら少し大きめ）。
            const MATH::Mat4 inverse = MATH::Inverse(runtime->GetTransform().GetWorldMatrix());
            MATH::Vec3 minimum{1.0e30f, 1.0e30f, 1.0e30f};
            MATH::Vec3 maximum{-1.0e30f, -1.0e30f, -1.0e30f};
            for (const MATH::Vec3& corner : corners) {
                const MATH::Vec4 local = inverse.TransformPoint({corner.x, corner.y, corner.z, 1.0f});
                minimum = {(std::min)(minimum.x, local.x), (std::min)(minimum.y, local.y),
                           (std::min)(minimum.z, local.z)};
                maximum = {(std::max)(maximum.x, local.x), (std::max)(maximum.y, local.y),
                           (std::max)(maximum.z, local.z)};
            }
            const MATH::Vec3 center = (minimum + maximum) * 0.5f;
            const MATH::Vec3 size = maximum - minimum;
            const MATH::Vec3 extent{settings.width, settings.height, settings.depth};
            bool overlaps = true;
            for (int axis = 0; axis < 3; ++axis) {
                overlaps &= std::abs(EDITOR::LookDevAxis(center, axis)) <
                            (EDITOR::LookDevAxis(extent, axis) + EDITOR::LookDevAxis(size, axis)) * 0.5f;
            }
            if (!overlaps) {
                continue;
            }
            settings.cuts.push_back({center, size});
            WriteShape(*component, SanitizeProceduralMeshSettings(settings));
            changed.push_back(target.id.value);
        }
        if (changed.empty()) {
            return false;
        }
        if (!keepCutter) {
            std::erase_if(document.objects,
                          [&](const SceneObjectData& object) { return cutterTree.contains(object.id.value); });
        }
        return true;
    });
    if (!done) {
        return EDITOR::OperationFailure("NO_OVERLAP", TEXT::Localize("ui.lookDev.cutOutNothing"));
    }
    return EDITOR::OperationOk("Shapes cut.", {{"changedIds", std::move(changed)}, {"cutterRemoved", !keepCutter}});
}

void DocumentSceneEditorController::DrawLookDevOpeningsSection(DocumentSceneBase& scene) {
    const SceneObjectId active = context_.selection.GetActiveObjectId();
    const GameObject* object =
        context_.selection.GetSelectedObjectCount() == 1u ? scene.GetWorld().FindObject(active) : nullptr;
    const ProceduralMeshComponent* shape =
        object != nullptr ? object->GetComponent<ProceduralMeshComponent>() : nullptr;
    if (shape == nullptr || shape->GetSettings().kind != ProceduralMeshKind::Box) {
        return;
    }
    const ProceduralMeshSettings& settings = shape->GetSettings();
    ImGui::SeparatorText(TEXT::Localize("ui.lookDev.openings").c_str());
    const bool full = settings.cuts.size() >= kMaximumProceduralMeshCuts;
    ImGui::BeginDisabled(full);
    const int thin = EDITOR::LookDevThinAxis(settings, object->GetTransform().scale);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    // 床は四角と丸の穴、壁は門・窓・丸窓。
    struct OpeningButton {
        EDITOR::LookDevOpeningKind kind;
        const char* labelKey;
        const char* hintKey;
    };
    constexpr OpeningButton kFloorButtons[] = {
        {EDITOR::LookDevOpeningKind::Hole, "ui.lookDev.openingHole", "ui.lookDev.openingHoleHint"},
        {EDITOR::LookDevOpeningKind::RoundHole, "ui.lookDev.openingRoundHole", "ui.lookDev.openingRoundHoleHint"},
    };
    constexpr OpeningButton kWallButtons[] = {
        {EDITOR::LookDevOpeningKind::Door, "ui.lookDev.openingDoor", "ui.lookDev.openingDoorHint"},
        {EDITOR::LookDevOpeningKind::Window, "ui.lookDev.openingWindow", "ui.lookDev.openingWindowHint"},
        {EDITOR::LookDevOpeningKind::RoundWindow, "ui.lookDev.openingRoundWindow", "ui.lookDev.openingRoundWindowHint"},
    };
    const std::span<const OpeningButton> buttons =
        thin == 1 ? std::span<const OpeningButton>(kFloorButtons) : std::span<const OpeningButton>(kWallButtons);
    const float width = (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(buttons.size() - 1u)) /
                        static_cast<float>(buttons.size());
    for (size_t index = 0; index < buttons.size(); ++index) {
        if (index > 0u) {
            ImGui::SameLine();
        }
        ImGui::PushID(static_cast<int>(index));
        if (EDITOR::ActionButton(TEXT::Localize(buttons[index].labelKey).c_str(), "##opening",
                                 EDITOR::EditorButtonTone::Neutral, ImVec2(width, 0.0f),
                                 TEXT::Localize(buttons[index].hintKey).c_str())) {
            RunLookDevOperation("procedural.opening",
                                {{"action", "add"},
                                 {"objectId", active.value},
                                 {"kind", TEXT::EnumToName(EDITOR::kLookDevOpeningKindNames, buttons[index].kind)}});
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    const EDITOR::LookDevBoxFrame frame = EDITOR::LookDevFrameFromWorld(object->GetTransform().GetWorldMatrix());
    for (size_t index = 0; index < settings.cuts.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        const ProceduralMeshCut& cut = settings.cuts[index];
        const std::string label =
            TEXT::Localize(cut.roundAxis >= 0 ? "ui.lookDev.openingRoundName" : "ui.lookDev.openingName") + " " +
            std::to_string(index + 1u) + "  " + EDITOR::FormatLookDevSize(frame, cut.size, false);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
        if (EDITOR::IconButton(EDITOR::EditorGlyph::Delete, "##remove", EDITOR::EditorButtonTone::Quiet,
                               ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()),
                               TEXT::Localize("ui.lookDev.openingRemove").c_str())) {
            RunLookDevOperation("procedural.opening",
                                {{"action", "remove"}, {"objectId", active.value}, {"index", index}});
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.cutOut").c_str(), "##cutOut", EDITOR::EditorButtonTone::Neutral,
                             ImVec2(-1.0f, 0.0f), TEXT::Localize("ui.lookDev.cutOutHint").c_str())) {
        RunLookDevOperation("procedural.subtract", {{"cutterId", active.value}});
    }
}
#endif

} // namespace HIKARI
