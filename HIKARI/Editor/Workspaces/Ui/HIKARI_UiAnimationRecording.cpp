#include "Editor/Workspaces/Ui/HIKARI_UiAnimationEditor.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }
std::string SlotFor(GameObject* object, GameObject* root) {
    if (!object || !root)
        return {};
    if (object == root)
        return "self";
    auto* parent = object->GetParent();
    while (parent && parent != root && !parent->GetComponent<UiAnimatorComponent>())
        parent = parent->GetParent();
    const auto* target = object->GetComponent<UiAnimationTargetComponent>();
    return parent == root && target ? target->GetSlot() : std::string{};
}
} // namespace
UiAnimationTrack& UiAnimationEditor::EnsureTrack(UiAnimationClip& clip, std::string_view slot,
                                                 UiMotionProperty property) {
    for (auto& track : clip.tracks)
        if (track.slot == slot && track.property == property)
            return track;
    UiAnimationTrack track;
    for (const auto& t : clip.tracks)
        track.id = std::max(track.id, t.id);
    ++track.id;
    track.slot = slot;
    track.property = property;
    track.curve.value = UiMotionProperties()[static_cast<size_t>(property)].initial;
    clip.tracks.push_back(std::move(track));
    return clip.tracks.back();
}
std::string UiAnimationEditor::TargetName(DocumentSceneBase& scene, std::string_view slot) const {
    auto* root = scene.GetWorld().FindObject(root_);
    if (root)
        for (const auto& object : scene.GetWorld().GetObjects())
            if (object && SlotFor(object.get(), root) == slot)
                return object->GetName();
    return std::string(L("uianim.missingTarget")) + " · " + std::string(slot);
}
bool UiAnimationEditor::BeginPoseDrag(DocumentSceneBase& scene, SceneObjectId id) {
    if (!document_.IsOpen() || scene.IsRuntimePlayActive())
        return false;
    auto* object = scene.GetWorld().FindObject(id);
    const auto slot = SlotFor(object, scene.GetWorld().FindObject(root_));
    const auto& clips = document_.Profile().clips;
    const auto clip = std::find_if(clips.begin(), clips.end(), [&](const auto& c) { return c.id == clip_; });
    if (slot.empty() || clip == clips.end()) {
        message_ = L("uianim.recordTargetHelp");
        return false;
    }
    playing_ = false;
    poseDrag_ = PoseDrag{document_.Profile(), document_.Profile(), slot, document_.Revision(),
                         scene.GetSceneDocumentRevision()};
    return true;
}
void UiAnimationEditor::UpdatePoseDrag(DocumentSceneBase& scene, float dx, float dy, float sx, float sy) {
    if (!poseDrag_)
        return;
    if (poseDrag_->revision != document_.Revision() || poseDrag_->sceneRevision != scene.GetSceneDocumentRevision()) {
        FinishPoseDrag(scene, nullptr);
        return;
    }
    auto& edit = *poseDrag_;
    edit.draft = edit.before;
    auto clip =
        std::find_if(edit.draft.clips.begin(), edit.draft.clips.end(), [&](const auto& c) { return c.id == clip_; });
    if (clip == edit.draft.clips.end())
        return;
    const auto key = [&](UiMotionProperty property, float delta, bool multiply) {
        if (delta == (multiply ? 1.f : 0.f))
            return;
        auto* track = &EnsureTrack(*clip, edit.slot, property);
        const auto& descriptor = UiMotionProperties()[static_cast<size_t>(property)];
        const float initial = track->curve.Evaluate(time_);
        if (track->curve.keys.empty() && time_ > 0)
            SetScalarCurveKey(track->curve, 0, initial);
        SetScalarCurveKey(
            track->curve, time_,
            std::clamp(multiply ? initial * delta : initial + delta, descriptor.minimum, descriptor.maximum));
    };
    key(UiMotionProperty::OffsetX, dx, false);
    key(UiMotionProperty::OffsetY, dy, false);
    key(UiMotionProperty::ScaleX, sx, true);
    key(UiMotionProperty::ScaleY, sy, true);
    PreviewAsset(scene, root_, edit.draft, clip->name, time_, false);
}
void UiAnimationEditor::FinishPoseDrag(DocumentSceneBase& scene, EditorOperationRegistry* operations) {
    if (!poseDrag_)
        return;
    const auto edit = std::move(*poseDrag_);
    poseDrag_.reset();
    if (operations && edit.revision == document_.Revision() && edit.sceneRevision == scene.GetSceneDocumentRevision()) {
        const auto result = operations->Execute("ui.animation.edit", {{"asset", WriteUiAnimationAsset(edit.draft)}});
        message_ = result.ok ? std::string{} : result.message;
        document_.SealMerge();
    }
    for (const auto& clip : document_.Profile().clips)
        if (clip.id == clip_) {
            Preview(scene, root_, clip.name, time_, false);
            break;
        }
}
void UiAnimationEditor::DrawPoseInspector(DocumentSceneBase& scene, EditorContext& context,
                                          EditorOperationRegistry& operations) {
    const auto slot =
        SlotFor(scene.GetWorld().FindObject(context.selection.GetActiveObjectId()), scene.GetWorld().FindObject(root_));
    auto asset = document_.Profile();
    auto clip = std::find_if(asset.clips.begin(), asset.clips.end(), [&](const auto& c) { return c.id == clip_; });
    if (slot.empty() || clip == asset.clips.end()) {
        ImGui::TextWrapped("%s", L("uianim.recordTargetHelp"));
        return;
    }
    ImGui::TextWrapped("%s", L("uianim.recordHelp"));
    ImGui::Text("%.3f s", time_);
    bool changed = false;
    for (const auto& property : UiMotionProperties()) {
        auto track = std::find_if(clip->tracks.begin(), clip->tracks.end(),
                                  [&](const auto& t) { return t.slot == slot && t.property == property.property; });
        float value = track == clip->tracks.end() ? property.initial : track->curve.Evaluate(time_);
        const float initial = value;
        if (ImGui::DragFloat(L(property.label), &value, .01f, property.minimum, property.maximum, "%.3f")) {
            auto& channel = EnsureTrack(*clip, slot, property.property);
            if (channel.curve.keys.empty() && time_ > 0)
                SetScalarCurveKey(channel.curve, 0, initial);
            SetScalarCurveKey(channel.curve, time_, value);
            changed = true;
        }
    }
    if (changed) {
        const auto r = operations.Execute("ui.animation.edit", {{"asset", WriteUiAnimationAsset(asset)},
                                                                {"mergeGroup", uint64_t(ImGui::GetActiveID())}});
        message_ = r.ok ? std::string{} : r.message;
    }
    if (!ImGui::IsAnyItemActive())
        document_.SealMerge();
}
void UiAnimationEditor::DrawCanvasTools(DocumentSceneBase& scene, EditorContext& context,
                                        EditorOperationRegistry& operations) {
    ImGui::BeginDisabled(scene.IsRuntimePlayActive());
    if (ImGui::RadioButton(L("uianim.layoutMode"), !autoKey_ && !preview_)) {
        StopPreview();
        autoKey_ = false;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_.IsOpen());
    if (ImGui::RadioButton(L("uianim.recordMode"), autoKey_)) {
        UiAnimationTrack target;
        if (AssignTarget(scene, context.selection.GetActiveObjectId(), operations, target)) {
            autoKey_ = true;
            for (const auto& clip : document_.Profile().clips)
                if (clip.id == clip_) {
                    Preview(scene, root_, clip.name, time_, false);
                    break;
                }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(L("uianim.previewStates")))
        ImGui::OpenPopup("UiPreviewStates");
    if (ImGui::BeginPopup("UiPreviewStates")) {
        for (const auto& clip : document_.Profile().clips)
            if (ImGui::MenuItem(clip.name.c_str())) {
                autoKey_ = false;
                if (!Preview(scene, root_, clip.name, 0, true))
                    message_ = L("uianim.previewTargetMissing");
            }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(L("uianim.saveRelated"))) {
        bool saved = true;
        if (document_.IsOpen() && document_.IsDirty()) {
            const auto r = operations.Execute("ui.animation.save", {});
            saved = r.ok;
            message_ = r.message;
        }
        if (saved) {
            const auto r = operations.Execute("scene.save", {});
            message_ = r.message;
        }
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", L(autoKey_ ? "uianim.recordHelp" : preview_ ? "uianim.previewHelp" : "uianim.layoutHelp"));
}
} // namespace HIKARI::EDITOR
