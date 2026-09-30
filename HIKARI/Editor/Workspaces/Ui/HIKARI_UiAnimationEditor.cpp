#include "Editor/Workspaces/Ui/HIKARI_UiAnimationEditor.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "HIKARI/Project/FrameContext.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }
bool Button(EditorGlyph glyph, const char* id, const char* label) {
    return IconButton(glyph, id, EditorButtonTone::Quiet, {26, 26}, L(label));
}
template <class Items>
uint64_t NextId(const Items& items) {
    uint64_t result = 1;
    for (const auto& item : items)
        result = std::max(result, item.id + 1);
    return result;
}
bool EditName(const char* id, std::string& text) {
    char buffer[256]{};
    std::memcpy(buffer, text.data(), std::min(text.size(), sizeof(buffer) - 1));
    if (!ImGui::InputText(id, buffer, sizeof(buffer)))
        return false;
    text = buffer;
    return true;
}
} // namespace
bool UiAnimationEditor::Open(AssetDatabase& db, const AssetGuid& guid) {
    if (!document_.Open(db, guid, message_))
        return false;
    clip_ = track_ = 0;
    selectedKeys_.clear();
    StopPreview();
    return true;
}
void UiAnimationEditor::StopPreview() {
    poseDrag_.reset();
    keyMarquee_.reset();
    preview_ = playing_ = false;
    player_.Reset();
    previewRevision_ = ~uint64_t(0);
    previewWorld_ = 0;
    previewHandle_ = {};
}
bool UiAnimationEditor::TravelHistory(bool redo) {
    const bool changed = redo ? document_.Redo() : document_.Undo();
    if (changed) {
        selectedKeys_.clear();
        drag_.reset();
        keyMarquee_.reset();
    }
    return changed;
}
bool UiAnimationEditor::Preview(DocumentSceneBase& scene, SceneObjectId root, std::string_view clip, float seconds,
                                bool play) {
    return PreviewAsset(scene, root, document_.Profile(), clip, seconds, play);
}
bool UiAnimationEditor::PreviewAsset(DocumentSceneBase& scene, SceneObjectId root, const UiAnimationAsset& source,
                                     std::string_view clip, float seconds, bool play) {
    auto* object = scene.GetWorld().FindObject(root);
    if (!object || !document_.IsOpen() || scene.IsRuntimePlayActive() || !std::isfinite(seconds))
        return false;
    auto asset = std::make_shared<UiAnimationAsset>(source);
    if (!player_.Play(scene.GetWorld(), *object, asset, clip))
        return false;
    root_ = root;
    for (const auto& c : asset->clips)
        if (c.name == clip)
            clip_ = c.id;
    time_ = seconds;
    playing_ = play;
    preview_ = true;
    previewRevision_ = document_.Revision();
    previewWorld_ = scene.GetWorld().GetRuntimeIdentity();
    previewHandle_ = object->GetRuntimeHandle();
    player_.Seek(scene.GetWorld(), object->GetRuntimeHandle(), seconds);
    player_.Pause(object->GetRuntimeHandle(), !play);
    return true;
}
void UiAnimationEditor::Tick(DocumentSceneBase& scene, float delta) {
    if (!preview_)
        return;
    if (scene.IsRuntimePlayActive() || previewWorld_ != scene.GetWorld().GetRuntimeIdentity()) {
        StopPreview();
        return;
    }
    auto* object = scene.GetWorld().FindObject(root_);
    if (!object || object->GetRuntimeHandle() != previewHandle_) {
        StopPreview();
        return;
    }
    const auto& clips = document_.Profile().clips;
    const auto clip = std::find_if(clips.begin(), clips.end(), [&](const auto& c) { return c.id == clip_; });
    if (clip == clips.end()) {
        StopPreview();
        return;
    }
    if (previewRevision_ != document_.Revision()) {
        if (!Preview(scene, root_, clip->name, time_, playing_)) {
            StopPreview();
            return;
        }
    }
    FrameContext frame{};
    frame.unscaledDt = frame.gameDt = playing_ ? delta : 0;
    player_.Tick(scene.GetWorld(), frame, false);
    const auto status = player_.GetStatus(object->GetRuntimeHandle());
    time_ = status.seconds;
    if (playing_ && !status.playing && loop_) {
        player_.Seek(scene.GetWorld(), object->GetRuntimeHandle(), 0);
        player_.Pause(object->GetRuntimeHandle(), false);
    } else
        playing_ = status.playing;
}
void UiAnimationEditor::BindCommands(EditorCommandRouter& commands) {
    if (!focused_ && !IsRecording())
        return;
    commands.BindOperation(EditorCommandId::SaveDocument, L("uix.save"), document_.IsOpen(), "ui.animation.save");
    commands.BindOperation(EditorCommandId::Undo, L("uix.undo"), document_.CanUndo(), "ui.animation.undo");
    commands.BindOperation(EditorCommandId::Redo, L("uix.redo"), document_.CanRedo(), "ui.animation.redo");
}
void UiAnimationEditor::BindToSelection(DocumentSceneBase& scene, EditorContext& context,
                                        EditorOperationRegistry& operations) {
    const auto id = context.selection.GetActiveObjectId();
    if (!document_.Guid().IsValid() || !id.value)
        return;
    auto* object = scene.GetWorld().FindObject(id);
    if (!object)
        return;
    if (!object->GetComponent<UiAnimatorComponent>()) {
        const auto r =
            operations.Execute("component.add", {{"objectId", id.value}, {"componentType", "UiAnimatorComponent"}});
        if (!r.ok) {
            message_ = r.message;
            return;
        }
    }
    for (const auto& data : scene.GetSceneDocument().objects)
        if (data.id == id)
            for (const auto& component : data.components)
                if (component.type == "UiAnimatorComponent") {
                    const auto r = operations.Execute(
                        "authoring.transaction",
                        {{"commands", nlohmann::json::array({{{"objectId", id.value},
                                                              {"componentId", component.id.value},
                                                              {"propertyId", "animationAssetGuid"},
                                                              {"value", document_.Guid().value}}})}});
                    message_ = r.ok ? std::string{} : r.message;
                    if (r.ok)
                        root_ = id;
                    return;
                }
}
bool UiAnimationEditor::AssignTarget(DocumentSceneBase& scene, SceneObjectId id, EditorOperationRegistry& operations,
                                     UiAnimationTrack& track) {
    auto* selected = scene.GetWorld().FindObject(id);
    auto* root = scene.GetWorld().FindObject(root_);
    if (!selected || !root ||
        (!selected->GetComponent<UiRectComponent>() && !selected->GetComponent<UiCanvasComponent>())) {
        message_ = L("uianim.selectRoot");
        return false;
    }
    if (selected == root) {
        track.slot = "self";
        return true;
    }
    auto* parent = selected->GetParent();
    while (parent && parent != root && !parent->GetComponent<UiAnimatorComponent>())
        parent = parent->GetParent();
    if (parent != root) {
        message_ = L("uianim.outsideRoot");
        return false;
    }
    auto* target = selected->GetComponent<UiAnimationTargetComponent>();
    if (target && !target->GetSlot().empty() && target->GetSlot() != "self") {
        track.slot = target->GetSlot();
        return true;
    }
    if (!target) {
        const auto r = operations.Execute("component.add",
                                          {{"objectId", id.value}, {"componentType", "UiAnimationTargetComponent"}});
        if (!r.ok) {
            message_ = r.message;
            return false;
        }
    }
    const auto slot = "element-" + std::to_string(id.value);
    for (const auto& object : scene.GetSceneDocument().objects)
        if (object.id == id)
            for (const auto& component : object.components)
                if (component.type == "UiAnimationTargetComponent") {
                    const auto r =
                        operations.Execute("authoring.transaction",
                                           {{"commands", nlohmann::json::array({{{"objectId", id.value},
                                                                                 {"componentId", component.id.value},
                                                                                 {"propertyId", "slot"},
                                                                                 {"value", slot}}})}});
                    message_ = r.message;
                    if (r.ok)
                        track.slot = slot;
                    return r.ok;
                }
    return false;
}
void UiAnimationEditor::Draw(DocumentSceneBase& scene, EditorContext& context, EditorOperationRegistry& operations) {
    if (!ImGui::Begin(TEXT::LocalizeWindow("uianim.timeline", "UiWorkspace/Animation").c_str())) {
        focused_ = false;
        ImGui::End();
        return;
    }
    focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::BeginDisabled(scene.IsRuntimePlayActive());
    if (Button(EditorGlyph::Add, "new", "uianim.new"))
        ImGui::OpenPopup("animationNew");
    if (ImGui::BeginPopup("animationNew")) {
        for (const auto* preset : {"panel", "button", "empty"}) {
            const auto label = std::string("uianim.") + preset;
            if (ImGui::Selectable(L(label.c_str()))) {
                const auto result = operations.Execute("ui.animation.new", {{"preset", preset}});
                message_ = result.message;
                if (result.ok) {
                    StopPreview();
                    clip_ = track_ = 0;
                    selectedKeys_.clear();
                    root_ = context.selection.GetActiveObjectId();
                    autoKey_ = false;
                    auto* selected = scene.GetWorld().FindObject(root_);
                    if (selected &&
                        (selected->GetComponent<UiRectComponent>() || selected->GetComponent<UiCanvasComponent>())) {
                        const auto saved = operations.Execute("ui.animation.save", {});
                        message_ = saved.message;
                        if (saved.ok)
                            BindToSelection(scene, context, operations);
                    }
                }
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (Button(EditorGlyph::Folder, "open", "uianim.open"))
        ImGui::OpenPopup("animationOpen");
    if (ImGui::BeginPopup("animationOpen")) {
        ImGui::InputTextWithHint("##search", L("uix.search"), search_, sizeof(search_));
        for (const auto* record : scene.GetAssetDatabase().CollectByType(AssetType::UiAnimation)) {
            if (TEXT::ToLowerAsciiCopy(record->displayName).find(TEXT::ToLowerAsciiCopy(search_)) == std::string::npos)
                continue;
            if (ImGui::Selectable((record->displayName + "##" + record->guid.value).c_str()))
                Open(scene.GetAssetDatabase(), record->guid);
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_.IsOpen());
    if (Button(EditorGlyph::Save, "saveAnimation", "uix.save")) {
        const auto r = operations.Execute("ui.animation.save", {});
        message_ = r.message;
    }
    ImGui::SameLine();
    if (Button(EditorGlyph::SaveAs, "copyAnimation", "uianim.saveAs")) {
        const auto r = operations.Execute("ui.animation.save", {{"saveAs", true}});
        message_ = r.message;
    }
    ImGui::SameLine();
    if (Button(EditorGlyph::Revert, "discardAnimation", "uianim.discard")) {
        if (document_.Guid().IsValid())
            document_.Open(scene.GetAssetDatabase(), document_.Guid(), message_, true);
        else
            document_.New(UiAnimationAsset{}, message_, true);
        StopPreview();
        selectedKeys_.clear();
        clip_ = track_ = 0;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_.Guid().IsValid() || !context.selection.GetActiveObjectId().value);
    if (IconTextButton(EditorGlyph::Dependency, L("uianim.bind"), "bind", EditorButtonTone::Quiet))
        BindToSelection(scene, context, operations);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_.CanUndo());
    if (Button(EditorGlyph::Undo, "undoMotion", "uix.undo"))
        operations.Execute("ui.animation.undo", {});
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_.CanRedo());
    if (Button(EditorGlyph::Redo, "redoMotion", "uix.redo"))
        operations.Execute("ui.animation.redo", {});
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());
    if (!document_.IsOpen()) {
        ImGui::TextWrapped("%s", L("uianim.start"));
        ImGui::EndDisabled();
        ImGui::End();
        return;
    }
    if (auto* root = scene.GetWorld().FindObject(root_))
        ImGui::Text("%s: %s", L("uianim.previewRoot"), root->GetName().c_str());
    else
        ImGui::TextUnformatted(L("uianim.selectRoot"));
    ImGui::SameLine();
    if (Button(EditorGlyph::Object, "previewRoot", "uianim.useSelection")) {
        StopPreview();
        root_ = context.selection.GetActiveObjectId();
    }
    auto asset = document_.Profile();
    bool changed = false;
    ImGui::SetNextItemWidth(180);
    changed |= EditName("##AnimationName", asset.name);
    ImGui::SameLine();
    ImGui::TextUnformatted(document_.IsDirty() ? "*" : "");
    ImGui::SameLine();
    if (Button(EditorGlyph::Add, "clipAdd", "uianim.addClip")) {
        UiAnimationClip clip;
        clip.id = NextId(asset.clips);
        clip.name = "Clip " + std::to_string(clip.id);
        asset.clips.push_back(clip);
        clip_ = clip.id;
        changed = true;
    }
    ImGui::SameLine();
    if (!clip_ && !asset.clips.empty())
        clip_ = asset.clips.front().id;
    auto clip = std::find_if(asset.clips.begin(), asset.clips.end(), [&](const auto& c) { return c.id == clip_; });
    ImGui::SetNextItemWidth(155);
    if (ImGui::BeginCombo("##Clip", clip != asset.clips.end() ? clip->name.c_str() : "")) {
        for (const auto& item : asset.clips)
            if (ImGui::Selectable(item.name.c_str(), item.id == clip_)) {
                clip_ = item.id;
                track_ = 0;
                selectedKeys_.clear();
                time_ = 0;
                StopPreview();
            }
        ImGui::EndCombo();
        clip = std::find_if(asset.clips.begin(), asset.clips.end(), [&](const auto& c) { return c.id == clip_; });
    }
    if (clip != asset.clips.end()) {
        ImGui::SameLine();
        if (Button(EditorGlyph::Delete, "clipDelete", "uianim.deleteClip")) {
            asset.clips.erase(clip);
            changed = true;
            clip_ = 0;
            StopPreview();
        } else {
            ImGui::SetNextItemWidth(150);
            changed |= EditName("##ClipName", clip->name);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            float duration = clip->duration;
            if (ImGui::DragFloat(L("uianim.duration"), &duration, .01f, .01f, 3600.f, "%.2f s")) {
                clip->duration = duration;
                for (auto& track : clip->tracks)
                    for (auto& key : track.curve.keys)
                        key.time = std::min(key.time, duration);
                for (auto& track : clip->tracks)
                    NormalizeScalarCurve(track.curve);
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110);
            const auto triggers = UiMotionTriggerNames();
            const auto triggerLabel = [](const char* name) {
                return TEXT::Localize(std::string("uianim.trigger.") + name);
            };
            if (ImGui::BeginCombo("##Trigger", triggerLabel(triggers[static_cast<size_t>(clip->trigger)]).c_str())) {
                for (size_t i = 0; i < triggers.size(); ++i)
                    if (ImGui::Selectable(triggerLabel(triggers[i]).c_str())) {
                        clip->trigger = static_cast<UiMotionTrigger>(i);
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            changed |= ImGui::Checkbox(L("uianim.repeat"), &clip->loop);
            if (clip->trigger == UiMotionTrigger::Close)
                clip->loop = false;
            if (Button(playing_ ? EditorGlyph::Pause : EditorGlyph::Play, "motionPlay", "uianim.preview")) {
                if (playing_) {
                    playing_ = false;
                    if (auto* root = scene.GetWorld().FindObject(root_))
                        player_.Pause(root->GetRuntimeHandle(), true);
                } else if (!Preview(scene, root_.value ? root_ : context.selection.GetActiveObjectId(), clip->name,
                                    time_ >= clip->duration ? 0 : time_, true))
                    message_ = L("uianim.previewTargetMissing");
            }
            ImGui::SameLine();
            if (Button(EditorGlyph::Stop, "motionStop", "uix.stop")) {
                StopPreview();
                time_ = 0;
            }
            ImGui::SameLine();
            ImGui::Checkbox(L("uianim.previewLoop"), &loop_);
            ImGui::SameLine();
            ImGui::Checkbox(L("uianim.autoKey"), &autoKey_);
            ImGui::SameLine();
            ImGui::Checkbox(L("uianim.curve"), &curve_);
            ImGui::SameLine();
            ImGui::Checkbox(L("uix.snap"), &snap_);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##Playhead", &time_, 0, clip->duration, "%.3f s")) {
                if (!Preview(scene, root_.value ? root_ : context.selection.GetActiveObjectId(), clip->name, time_,
                             false))
                    message_ = L("uianim.previewTargetMissing");
            }
            if (auto target = std::find_if(clip->tracks.begin(), clip->tracks.end(),
                                           [&](const auto& t) { return t.id == track_; });
                target != clip->tracks.end()) {
                if (IconTextButton(EditorGlyph::Object, L("uianim.useSelectedTarget"), "selectedTrackTarget",
                                   EditorButtonTone::Quiet))
                    changed |= AssignTarget(scene, context.selection.GetActiveObjectId(), operations, *target);
            }
            const float beforeTime = time_;
            DrawTimeline(scene, context, operations, asset, *clip, changed);
            if (time_ != beforeTime)
                Preview(scene, root_.value ? root_ : context.selection.GetActiveObjectId(), clip->name, time_, false);
        }
    }
    if (changed) {
        const auto result = operations.Execute("ui.animation.edit", {{"asset", WriteUiAnimationAsset(asset)},
                                                                     {"mergeGroup", uint64_t(ImGui::GetActiveID())}});
        message_ = result.ok ? std::string{} : result.message;
    }
    if (!ImGui::IsAnyItemActive())
        document_.SealMerge();
    ImGui::EndDisabled();
    ImGui::End();
}
} // namespace HIKARI::EDITOR
