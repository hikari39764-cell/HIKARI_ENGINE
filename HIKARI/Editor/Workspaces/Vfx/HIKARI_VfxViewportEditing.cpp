#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include <algorithm>
#include <cmath>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
bool VfxWorkspaceController::DrawLayerGizmo(const EditorViewportRect& rect) {
#if defined(HIKARI_WITH_EDITOR)
    auto& state = timelineEdit_;
    const auto* layer = FindEmitter(selection_.emitterId);
    if (!layer || layer->kind == VFX::VfxLayerKind::Adjustment || !viewportHandle_.id.empty() ||
        ImGui::GetIO().KeyAlt || state.keyDragging || !state.draggedLayer.empty())
        return false;
    if (layerGizmoEditing_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        EndTimelineEdit(true);
        state.suppressDragUntilRelease = true;
        layerGizmoEditing_ = false;
        return true;
    }
    if (state.suppressDragUntilRelease) {
        if (!ImGui::IsMouseDown(0))
            state.suppressDragUntilRelease = false;
        return true;
    }
    VFX::VfxWorldTransform world, frame;
    if (!preview_.playbackService_.GetLayerPose(preview_.previewHandle_, layer->id, world, frame))
        return false;
    const bool autoKey = state.autoKey && layer->kind != VFX::VfxLayerKind::Particles;
    if (!layerGizmoEditing_) {
        const auto frameMatrix = MATH::Mat4::TRS(frame.position, frame.rotation, frame.scale);
        const auto worldMatrix = MATH::Mat4::TRS(world.position, world.rotation, world.scale);
        if (std::abs(frame.scale.x * frame.scale.y * frame.scale.z) < 0.000001f)
            return false;
        layerGizmoAnimation_ = MATH::Inverse(frameMatrix) * worldMatrix;
        const auto& base = layer->emitterTransform.transform;
        const auto r = base.rotationEulerDegrees * (3.14159265f / 180);
        const auto baseMatrix = MATH::Mat4::TRS(base.position, MATH::Quat::FromEulerXYZ(r.x, r.y, r.z), base.scale);
        layerGizmoFrame_ = autoKey ? frameMatrix : frameMatrix * MATH::Inverse(baseMatrix);
        // ゼロスケールの開始キーも基準位置から編集できる。
        if (std::abs(world.scale.x * world.scale.y * world.scale.z) < 0.000001f && !autoKey) {
            world = frame;
            layerGizmoAnimation_ = MATH::Mat4::Identity();
        }
        layerGizmoTransform_.position = world.position;
        layerGizmoTransform_.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(world.rotation);
        layerGizmoTransform_.scale = world.scale;
    }
    const auto edit = EditorTransformGizmo{}.DrawTransform(layerGizmoTransform_, preview_.cameraController_.GetCamera(),
                                                           layerGizmoState_, rect);
    if (edit.manipulating && !layerGizmoEditing_)
        BeginTimelineEdit();
    if (edit.changed) {
        layerGizmoTransform_ = edit.transform;
        auto matrix = MATH::Inverse(layerGizmoFrame_) *
                      MATH::Mat4::TRS(edit.transform.position, edit.rotation, edit.transform.scale);
        if (!autoKey)
            matrix = matrix * MATH::Inverse(layerGizmoAnimation_);
        VFX::VfxWorldTransform local;
        if (MATH::DecomposeTRS(matrix, local.position, local.rotation, local.scale)) {
            const auto id = layer->id;
            if (autoKey) {
                const bool rotate = layerGizmoState_.operation == EditorTransformGizmoOperation::Rotate;
                const bool scale = layerGizmoState_.operation == EditorTransformGizmoOperation::Scale;
                const auto value = rotate ? MATH::EulerXYZDegreesFromQuatNearest(
                                                local.rotation, layer->rotation.eulerDegrees.fixed.minimum)
                                   : scale ? local.scale
                                           : local.position;
                const float end = layer->activeDuration > 0 ? layer->startDelay + layer->activeDuration
                                                            : document_.Asset().durationSeconds;
                const float seconds = std::clamp(preview_.previewElapsedSeconds_, layer->startDelay, end);
                (void)SetLayerTransformKey(id,
                                           rotate  ? "rotation"
                                           : scale ? "scale"
                                                   : "position",
                                           seconds, value, statusMessage_, VfxTimelineEditState::mergeGroup);
            } else {
                auto next = document_.Asset();
                auto* target = FindEmitterMutable(next, id);
                auto& base = target->emitterTransform.transform;
                base.position = local.position;
                base.rotationEulerDegrees =
                    MATH::EulerXYZDegreesFromQuatNearest(local.rotation, base.rotationEulerDegrees);
                base.scale = local.scale;
                (void)ApplyAsset(std::move(next), statusMessage_, VfxTimelineEditState::mergeGroup);
            }
        }
    }
    if (layerGizmoEditing_ && !edit.manipulating)
        EndTimelineEdit(false);
    layerGizmoEditing_ = edit.manipulating;
    return edit.interacting;
#else
    (void)rect;
    return false;
#endif
}
} // namespace HIKARI::EDITOR
