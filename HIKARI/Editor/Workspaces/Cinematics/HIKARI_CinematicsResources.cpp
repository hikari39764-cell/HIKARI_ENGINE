#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
bool CinematicsWorkspaceController::DrawResources(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                  EditorContext& context) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    using namespace SEQUENCER;
    size_t unresolved = 0;
    for (const auto& binding : sequence.bindings) {
        SceneObjectId id;
        if (!previewBindings_.Resolve(sequence.bindings, binding.id, id) || !scene.GetWorld().FindObject(id))
            ++unresolved;
    }
    ImGui::TextWrapped("%s", TEXT::LocalizeCStr("cine.workflowHelp"));
    if (unresolved)
        ImGui::Text(TEXT::LocalizeCStr("cine.missingBindings"), static_cast<int>(unresolved));
    if (!ImGui::BeginTabBar("SequenceResources"))
        return false;
    if (ImGui::BeginTabItem(TEXT::LocalizeWindow("cine.shot", "镜头").c_str())) {
        if (IconTextButton(EditorGlyph::Add, TEXT::Localize("cine.createShot").c_str(), "newShot",
                           EditorButtonTone::Primary)) {
            CreateShot(sequence, spaceView_ ? directorViewPanel_.GetViewCamera() : scene.GetCamera());
            changed = true;
        }
        for (const auto& shot : sequence.cameraCutTrack.clips) {
            ImGui::PushID(static_cast<int>(shot.id));
            char duration[32];
            std::snprintf(duration, sizeof(duration), "  ·  %.1f s", shot.durationSeconds);
            const auto label = shot.camera.name + duration;
            if (ImGui::Selectable(label.c_str(), selectionKind_ == SelectionKind::Shot && selectedId_ == shot.id)) {
                selectedId_ = shot.id;
                selectionKind_ = SelectionKind::Shot;
                curveView_.selectedKey = -1;
                cursor_.Seek(shot.startTimeSeconds);
                previewEnabled_ = true;
            }
            ImGui::PopID();
        }
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete,
                                  ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
            changed |= DeleteSelectedClip(sequence);
        if (sequence.cameraCutTrack.clips.empty())
            EmptyState(TEXT::Localize("cine.startFrame").c_str(), TEXT::Localize("cine.createFirst").c_str());
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(TEXT::LocalizeWindow("cine.assets", "素材").c_str())) {
        static std::array<char, 128> search{};
        SearchField("cueSearch", TEXT::Localize("cine.searchAssets").c_str(), search.data(), search.size());
        const auto addCue = [&](const std::string& name, PresentationCueKind kind, const std::string& asset) {
            PresentationCueClip cue;
            for (const auto& c : sequence.presentationCueTrack.clips)
                cue.id = (std::max)(cue.id, c.id + 1);
            cue.name = name;
            cue.kind = kind;
            cue.startSeconds = cursor_.GetTimeSeconds();
            cue.durationSeconds = 3;
            if (kind == PresentationCueKind::Vfx)
                cue.vfxAsset.value = asset;
            else
                cue.soundPath = asset;
            sequence.presentationCueTrack.clips.push_back(cue);
            selectedId_ = cue.id;
            selectionKind_ = SelectionKind::Cue;
            changed = true;
        };
        if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("menu.workspace.vfx", "特效").c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto* record : scene.GetAssetDatabase().CollectByType(AssetType::VfxEffect)) {
                const auto name = GetAssetRecordDisplayName(*record);
                if (search[0] && name.find(search.data()) == std::string::npos)
                    continue;
                ImGui::PushID(record->guid.value.c_str());
                if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(0))
                    addCue(name, PresentationCueKind::Vfx, record->guid.value);
                BeginAssetDragSource(*record);
                ImGui::PopID();
            }
        }
        if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("cine.sound", "声音").c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto root = GetProjectContext().GetRoot();
            const bool refresh = ImGui::SmallButton(TEXT::LocalizeWindow("cine.refreshSound", "刷新声音素材").c_str());
            if (audioProjectRoot_ != root || refresh) {
                audioProjectRoot_ = root;
                audioAssets_.clear();
                std::error_code ec;
                for (std::filesystem::recursive_directory_iterator it(root / "Assets/Audio", ec), end; !ec && it != end;
                     it.increment(ec)) {
                    if (!it->is_regular_file(ec))
                        continue;
                    auto extension = it->path().extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (extension == ".wav" || extension == ".mp3" || extension == ".ogg")
                        audioAssets_.push_back(it->path());
                }
                std::sort(audioAssets_.begin(), audioAssets_.end());
            }
            for (const auto& path : audioAssets_) {
                const auto name = path.stem().string();
                if (search[0] && name.find(search.data()) == std::string::npos)
                    continue;
                const auto relative = path.lexically_relative(root).generic_string();
                ImGui::PushID(relative.c_str());
                if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(0))
                    addCue(name, PresentationCueKind::Audio, relative);
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("HIKARI_SEQUENCE_AUDIO", relative.c_str(), relative.size() + 1);
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();
            }
        }
        ImGui::TextDisabled(TEXT::Localize("cine.dropHint").c_str());
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(TEXT::LocalizeWindow("ui.vfx.text.Templates", "模板").c_str())) {
        if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("cine.presets", "完整演出").c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Selectable(TEXT::LocalizeWindow("cine.stagePreset", "主体展示 · 远景 / 环绕 / 推进").c_str())) {
                const float start = cursor_.GetTimeSeconds();
                for (int i = 0; i < 3; ++i) {
                    cursor_.Seek(start + i * 3.0f);
                    CreateShot(sequence, scene.GetCamera());
                    auto& shot = sequence.cameraCutTrack.clips.back();
                    shot.startTimeSeconds = start + i * 3.0f;
                    shot.camera.name = i == 0   ? TEXT::Localize("cine.establish")
                                       : i == 1 ? TEXT::Localize("cine.preset.orbit")
                                                : TEXT::Localize("cine.closingPush");
                    if (i > 0) {
                        CameraMotionEffect e;
                        e.kind = i == 1 ? CameraMotionKind::Orbit : CameraMotionKind::Dolly;
                        e.amount.keys = {{0, 0, 0, 0}, {3, i == 1 ? 65.0f : 2.0f, 0, 0}};
                        shot.camera.motion.push_back(e);
                    }
                }
                cursor_.Seek(start);
                changed = true;
            }
        }
        if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("cine.modules", "可复用模块").c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* names[]{TEXT::LocalizeCStr("cine.preset.dolly"), TEXT::LocalizeCStr("cine.preset.orbit"),
                                TEXT::LocalizeCStr("cine.preset.handheld"), TEXT::LocalizeCStr("cine.preset.impact")};
            const CameraMotionKind kinds[]{CameraMotionKind::Dolly, CameraMotionKind::Orbit, CameraMotionKind::Handheld,
                                           CameraMotionKind::Shake};
            for (int i = 0; i < 4; ++i)
                if (ImGui::Selectable(names[i])) {
                    auto* shot = SelectedShot(sequence);
                    if (!shot) {
                        CreateShot(sequence, scene.GetCamera());
                        shot = SelectedShot(sequence);
                    }
                    CameraMotionEffect e;
                    e.kind = kinds[i];
                    for (const auto& existing : shot->camera.motion)
                        e.id = (std::max)(e.id, existing.id + 1);
                    e.amount.value = i == 2 ? 0.025f : i == 3 ? 0.15f : 1.0f;
                    if (i < 2)
                        e.amount.keys = {{0, 0, 0, 0}, {shot->durationSeconds, i == 1 ? 90.0f : 2.0f, 0, 0}};
                    if (i == 3)
                        e.weight.keys = {{0, 1, 0, 0}, {0.4f, 0, 0, 0}};
                    shot->camera.motion.push_back(e);
                    changed = true;
                }
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(TEXT::LocalizeWindow("cine.bindings", "对象绑定").c_str())) {
        if (ImGui::Button(TEXT::LocalizeWindow("cine.addBinding", "添加对象槽").c_str())) {
            const auto name = "Subject." + std::to_string(AllocateSequenceBindingId(sequence.bindings).value);
            auto id = FindOrCreateSlotBinding(sequence.bindings, name, name);
            FindSequenceBinding(sequence.bindings, id)->required = false;
            changed = true;
        }
        ImGui::SameLine();
        const auto selected = context.selection.GetActiveObjectId();
        ImGui::BeginDisabled(!selected.value);
        if (ImGui::Button(TEXT::LocalizeCStr("cine.slotFromSelection"))) {
            const auto slotName = "Subject." + std::to_string(AllocateSequenceBindingId(sequence.bindings).value);
            const auto id = FindOrCreateSlotBinding(sequence.bindings, slotName, slotName);
            for (const auto& object : scene.GetSceneDocument().objects)
                if (object.id == selected)
                    FindSequenceBinding(sequence.bindings, id)->name = object.name;
            previewBindings_.Bind(id, selected);
            previewChanged_ = true;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", TEXT::LocalizeCStr("cine.bindingHelp"));
        for (auto& binding : sequence.bindings) {
            ImGui::PushID(static_cast<int>(binding.id.value));
            std::array<char, 128> name{};
            std::snprintf(name.data(), name.size(), "%s", binding.name.c_str());
            if (ImGui::InputText(TEXT::LocalizeWindow("ui.input.name", "名称").c_str(), name.data(), name.size())) {
                binding.name = name.data();
                changed = true;
            }
            SceneObjectId current;
            previewBindings_.Resolve(sequence.bindings, binding.id, current);
            std::string label = TEXT::Localize("cine.unbound");
            for (const auto& object : scene.GetSceneDocument().objects)
                if (object.id == current)
                    label = object.name;
            if (ImGui::BeginCombo(TEXT::LocalizeWindow("cine.previewObject", "预览对象").c_str(), label.c_str())) {
                static ImGuiTextFilter filter;
                if (ImGui::IsWindowAppearing()) {
                    filter.Clear();
                    ImGui::SetKeyboardFocusHere();
                }
                filter.Draw(TEXT::LocalizeCStr("uix.search"));
                for (const auto& object : scene.GetSceneDocument().objects) {
                    if (!filter.PassFilter(object.name.c_str()))
                        continue;
                    const auto objectLabel = object.name + "##" + std::to_string(object.id.value);
                    if (ImGui::Selectable(objectLabel.c_str(), object.id == current)) {
                        previewBindings_.Bind(binding.id, object.id);
                        previewChanged_ = true;
                        if (binding.targetKind == SequenceBindingTargetKind::SceneObject) {
                            binding.sceneObjectId = object.id;
                            changed = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(!selected.value);
            if (ImGui::SmallButton(TEXT::LocalizeCStr("cine.useSelection"))) {
                previewBindings_.Bind(binding.id, selected);
                previewChanged_ = true;
                if (binding.targetKind == SequenceBindingTargetKind::SceneObject) {
                    binding.sceneObjectId = selected;
                    changed = true;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!scene.GetWorld().FindObject(current));
            if (ImGui::SmallButton(TEXT::LocalizeCStr("cine.selectBoundObject")))
                context.selection.SelectObjectIds(scene.GetWorld(), {current});
            ImGui::EndDisabled();
            changed |= ImGui::Checkbox(TEXT::LocalizeWindow("cine.requiredRuntime", "运行时必须提供").c_str(),
                                       &binding.required);
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
#endif
    return changed;
}
} // namespace HIKARI::EDITOR
