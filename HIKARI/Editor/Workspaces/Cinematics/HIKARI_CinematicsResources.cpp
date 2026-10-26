#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Scene/Sequencer/Drivers/HIKARI_PresentationCueDriver.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Editor/Widgets/HIKARI_NumberField.h"
#include "Scene/Camera/HIKARI_CameraVolume.h"
#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
bool CinematicsWorkspaceController::DrawResources(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                  EditorContext& context, EditorOperationRegistry& operations) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    using namespace SEQUENCER;
    size_t unresolved = 0;
    for (const auto& binding : sequence.bindings) {
        SceneObjectId id;
        if (!previewBindings_.Resolve(sequence.bindings, binding.id, id) || !scene.GetWorld().FindObject(id))
            ++unresolved;
    }
    ImGui::TextWrapped("%s", TEXT::Localize("cine.workflowHelp").c_str());
    if (unresolved)
        ImGui::Text(TEXT::Localize("cine.missingBindings").c_str(), static_cast<int>(unresolved));
    // 窓が狭くても名前を省略しない（収まらない分は横へ送る）。
    if (!ImGui::BeginTabBar("SequenceResources", ImGuiTabBarFlags_FittingPolicyScroll))
        return false;
    if (ImGui::BeginTabItem(TEXT::LocalizeWindow("cine.shot", "镜头").c_str())) {
        // 镜头は視口（無ければ主ビュー）の今の視点から作る。空の時は案内の中に同じ操作を置く。
        const auto createShot = [&] {
            CreateShot(sequence, spaceView_ ? directorViewPanel_.GetViewCamera() : scene.GetCamera());
            changed = true;
        };
        if (!sequence.cameraCutTrack.clips.empty() &&
            IconTextButton(EditorGlyph::Add, TEXT::Localize("cine.createShot").c_str(), "newShot",
                           EditorButtonTone::Primary)) {
            createShot();
        }
        for (const auto& shot : sequence.cameraCutTrack.clips) {
            ImGui::PushID(static_cast<int>(shot.id));
            char duration[32];
            std::snprintf(duration, sizeof(duration), "  ·  %.1f s", shot.durationSeconds);
            const auto label = shot.camera.name + duration;
            if (ImGui::Selectable(label.c_str(), selectionKind_ == SelectionKind::Shot && selectedId_ == shot.id)) {
                selectedId_ = shot.id;
                selectionKind_ = SelectionKind::Shot;
                curveView_.editing.selection.clear();
                cursor_.Seek(shot.startTimeSeconds);
                previewEnabled_ = true;
            }
            ImGui::PopID();
        }
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete,
                                  ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
            changed |= DeleteSelectedClip(sequence);
        if (sequence.cameraCutTrack.clips.empty() &&
            EmptyState(TEXT::Localize("cine.startFrame").c_str(), TEXT::Localize("cine.createFirst").c_str(),
                       TEXT::Localize("cine.createShotFromView").c_str())) {
            createShot();
        }
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
            else {
                cue.soundPath = asset;
                // 置いた音は素材の長さぶんの片段にする。
                if (const float length = GetPresentationCueSoundSeconds(asset); length > 0.0f)
                    cue.durationSeconds = length;
            }
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
            // 名前と相対パスは UTF-8 で扱う（string() は ANSI 符号页になり、日本語の名前が化けるか例外になる）。
            for (const auto& path : audioAssets_) {
                const auto name = TEXT::PathToUtf8(path.stem());
                if (search[0] && name.find(search.data()) == std::string::npos)
                    continue;
                const auto relative = TEXT::PathToUtf8(path.lexically_relative(root));
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
            const char* names[]{
                TEXT::Localize("cine.preset.dolly").c_str(), TEXT::Localize("cine.preset.orbit").c_str(),
                TEXT::Localize("cine.preset.handheld").c_str(), TEXT::Localize("cine.preset.impact").c_str()};
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
    if (ImGui::BeginTabItem(TEXT::LocalizeWindow("cine.triggers", "触发区域").c_str())) {
        DrawTriggers(scene, operations);
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
        if (ImGui::Button(TEXT::Localize("cine.slotFromSelection").c_str())) {
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
        ImGui::TextWrapped("%s", TEXT::Localize("cine.bindingHelp").c_str());
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
                filter.Draw(TEXT::Localize("uix.search").c_str());
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
            if (ImGui::SmallButton(TEXT::Localize("cine.useSelection").c_str())) {
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
            if (ImGui::SmallButton(TEXT::Localize("cine.selectBoundObject").c_str()))
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
void CinematicsWorkspaceController::DrawTriggers(DocumentSceneBase& scene, EditorOperationRegistry& operations) {
#if defined(HIKARI_WITH_EDITOR)
    const auto L = [](const char* key) { return TEXT::Localize(key); };
    const AssetGuid& sequenceGuid = sequenceDocumentController_.GetDocument().GetAssetGuid();
    if (!IsEditingSequenceAsset() || !sequenceGuid.IsValid()) {
        EmptyState(L("cine.triggerNeedsAsset").c_str(), L("cine.triggerNeedsAssetHint").c_str());
        return;
    }
    const SceneObjectId player = FindScenePlayer(scene.GetSceneDocument());
    const auto camera = scene.GetSceneDocument().camera.defaultCameraObjectId;
    const GameObject* playerObject = scene.GetWorld().FindObject(player);
    ImGui::BeginDisabled(playerObject == nullptr || !camera);
    if (ActionButton(L("cine.newTrigger").c_str(), "##newTrigger", EditorButtonTone::Primary, ImVec2(-1.0f, 0.0f))) {
        const MATH::Vec3 position = CAMERA::WorldPosition(*playerObject);
        pendingStatusMessage_ =
            operations
                .Execute("camera_flow.edit",
                         {{"action", "create"},
                          {"name", L("cine.triggerName")},
                          {"position", {position.x, position.y, position.z}},
                          {"settings",
                           {{"sequenceAssetGuid", sequenceGuid.value},
                            {"subjectObjectId", SCENE::SERIALIZATION::WriteSceneObjectReference(player.value)},
                            {"cameraObjectId", SCENE::SERIALIZATION::WriteSceneObjectReference(camera->value)},
                            {"afterMode", ""}}}})
                .message;
    }
    ImGui::EndDisabled();
    if (playerObject == nullptr || !camera) {
        ImGui::TextDisabled("%s", L("cine.triggerNeedsPlayer").c_str());
    }
    // 終わった後の模式は、追う機位の方案の模式から選ぶ（空は今の模式のまま）。
    const auto* store = scene.GetWorld().Services().Find<GameplayCameraAssetStore>();
    std::vector<std::pair<SceneObjectId, nlohmann::json>> edits;
    std::vector<SceneObjectId> removed;
    scene.GetWorld().ForEachObjectWith<CameraFlowComponent>([&](GameObject& object, CameraFlowComponent& flow) {
        const CameraFlowSettings& settings = flow.Settings();
        if (settings.sequence != sequenceGuid) {
            return;
        }
        const SceneObjectId id = object.GetDocumentId();
        ImGui::PushID(static_cast<int>(id.value));
        ImGui::SeparatorText(object.GetName().c_str());
        nlohmann::json patch = nlohmann::json::object();
        MATH::Vec3 size = settings.halfExtents * 2.0f;
        ImGui::TextUnformatted(L("cine.triggerSize").c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (DragNumbers("##size", &size.x, 3, {.minimum = 0.1, .maximum = 1000.0, .step = 0.05})) {
            patch["halfExtents"] = {size.x * 0.5f, size.y * 0.5f, size.z * 0.5f};
        }
        const GameObject* cameraObject = scene.GetWorld().FindObject(settings.cameraObjectId);
        const auto* follow = cameraObject ? cameraObject->GetComponent<CameraFollowComponent>() : nullptr;
        std::string error;
        const auto profile = store && follow ? store->Load(follow->GameplayBinding().profile, error) : nullptr;
        const std::string current = settings.afterMode.empty() ? L("cine.triggerKeepMode") : settings.afterMode;
        if (ImGui::BeginCombo(L("cine.triggerAfterMode").c_str(), current.c_str())) {
            if (ImGui::Selectable(L("cine.triggerKeepMode").c_str(), settings.afterMode.empty())) {
                patch["afterMode"] = "";
            }
            if (profile) {
                for (const auto& mode : profile->modes) {
                    if (ImGui::Selectable(mode.name.c_str(), settings.afterMode == mode.id)) {
                        patch["afterMode"] = mode.id;
                    }
                }
            }
            ImGui::EndCombo();
        }
        bool repeat = settings.repeatOnReentry;
        if (ImGui::Checkbox(L("cine.triggerRepeat").c_str(), &repeat)) {
            patch["repeatOnReentry"] = repeat;
        }
        bool onExit = settings.triggerOnExit;
        if (ImGui::Checkbox(L("cine.triggerOnExit").c_str(), &onExit)) {
            patch["triggerOnExit"] = onExit;
        }
        if (ActionButton(L("cine.triggerRemove").c_str(), "##remove", EditorButtonTone::Danger)) {
            removed.push_back(id);
        } else if (!patch.empty()) {
            edits.emplace_back(id, std::move(patch));
        }
        ImGui::PopID();
    });
    // 場面を変える操作は一覧を描き終えてから流す（描いている途中で物体が作り直されないように）。
    for (auto& [id, patch] : edits) {
        const auto result = operations.Execute(
            "camera_flow.edit",
            {{"action", "update"}, {"objectId", id.value}, {"settings", std::move(patch)}, {"mergeKey", id.value}});
        if (!result.ok) {
            pendingStatusMessage_ = result.message;
        }
    }
    for (const SceneObjectId id : removed) {
        pendingStatusMessage_ =
            operations.Execute("camera_flow.edit", {{"action", "remove"}, {"objectId", id.value}}).message;
    }
#else
    (void)scene;
    (void)operations;
#endif
}

} // namespace HIKARI::EDITOR
