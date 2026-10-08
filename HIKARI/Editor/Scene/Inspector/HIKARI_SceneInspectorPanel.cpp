#include "Editor/Scene/Inspector/HIKARI_SceneInspectorPanel.h"

#include "Core/HIKARI_MathConfig.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
constexpr float kDegreesToRadians = MATH::kDegreesToRadians;

int ComponentCategoryRank(std::string_view category) {
    if (category == "Rendering")
        return 0;
    if (category == "Camera")
        return 1;
    if (category == "Gameplay")
        return 2;
    if (category == "Cinematics")
        return 3;
    return 4;
}

bool ComponentPresentationLess(const ComponentTypeInfo* lhs, const ComponentTypeInfo* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return rhs != nullptr;
    }
    const int lhsRank = ComponentCategoryRank(lhs->presentation.category);
    const int rhsRank = ComponentCategoryRank(rhs->presentation.category);
    if (lhsRank != rhsRank) {
        return lhsRank < rhsRank;
    }
    if (lhs->presentation.category != rhs->presentation.category) {
        return lhs->presentation.category < rhs->presentation.category;
    }
    return lhs->presentation.displayName < rhs->presentation.displayName;
}

bool MatchesComponent(const ComponentTypeInfo& info, std::string_view search) {
    if (search.empty()) {
        return true;
    }
    const std::string haystack =
        TEXT::ToLowerAsciiCopy(TEXT::Localize(info.presentation.displayName) + " " + info.presentation.displayName +
                               " " + info.presentation.category + " " + info.typeName);
    return haystack.find(TEXT::ToLowerAsciiCopy(search)) != std::string::npos;
}

std::string LocalizeComponentCategory(std::string_view category) {
    if (category == "Rendering") {
        return TEXT::Localize("ui.inspector.category.rendering");
    }
    if (category == "Camera") {
        return TEXT::Localize("ui.inspector.category.camera");
    }
    if (category == "Gameplay") {
        return TEXT::Localize("ui.inspector.category.gameplay");
    }
    if (category == "Cinematics") {
        return TEXT::Localize("ui.inspector.category.cinematics");
    }
    return std::string(category);
}

void SelectObject(EditorContext& context, GameObject& object) {
    if (World* world = object.GetWorld()) {
        context.selection.SelectObject(*world, &object);
    }
}

void DrawStatusMessage(std::string_view message, bool error) {
    if (message.empty()) {
        return;
    }
#if defined(HIKARI_WITH_EDITOR)
    const std::string text(message);
    StatusBadge(text.c_str(), error ? EditorStatusTone::Error : EditorStatusTone::Ready);
#else
    (void)error;
#endif
}
} // namespace

void SceneInspectorPanel::Draw(DocumentSceneBase& scene, EditorContext& context,
                               const SelectionSyncService& selectionSync, SceneObjectCommandService& commands,
                               bool* open) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.window.inspector", "Inspector").c_str(), open)) {
        ImGui::End();
        return;
    }
    DrawContents(scene, context, selectionSync, commands);
    ImGui::End();
#else
    (void)scene;
    (void)context;
    (void)selectionSync;
    (void)commands;
    (void)open;
#endif
}

void SceneInspectorPanel::DrawContents(DocumentSceneBase& scene, EditorContext& context,
                                       const SelectionSyncService& selectionSync, SceneObjectCommandService& commands) {
#if defined(HIKARI_WITH_EDITOR)
    GameObject* selected = context.selection.selectedObject;
    SceneObjectData* target =
        selected != nullptr ? selectionSync.FindDocumentObjectByRuntime(scene, selected) : nullptr;
    if (selected == nullptr || target == nullptr) {
        EmptyState(TEXT::Localize("ui.inspector.emptyTitle").c_str(), TEXT::Localize("ui.inspector.emptyHint").c_str());
        return;
    }

    ImGui::PushID(static_cast<int>(target->id.value));
    PanelTitle(target->name.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(TEXT::Localize("ui.inspector.objectHeaderTooltip").c_str(), target->name.c_str(),
                          static_cast<unsigned long long>(target->id.value));
    }
    if (ImGui::BeginPopupContextItem("ObjectHeaderContext")) {
        DrawObjectContextMenu(scene, context, selectionSync, commands, *selected);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    const float menuWidth = ImGui::GetFrameHeight();
    ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - menuWidth));
    if (IconButton(EditorGlyph::More, "ObjectActionsButton", EditorButtonTone::Quiet, ImVec2(menuWidth, menuWidth),
                   TEXT::Localize("ui.inspector.objectActions").c_str())) {
        ImGui::OpenPopup("ObjectActions");
    }
    if (ImGui::BeginPopup("ObjectActions")) {
        DrawObjectContextMenu(scene, context, selectionSync, commands, *selected);
        ImGui::EndPopup();
    }
    ImGui::PopID();

    if (context.selection.GetSelectedObjectCount() > 1u) {
        ImGui::TextDisabled(TEXT::Localize("ui.inspector.multiSelectFormat").c_str(),
                            static_cast<int>(context.selection.GetSelectedObjectCount()));
    }
    if (target->editorLocked) {
        StatusText(TEXT::Localize("ui.inspector.lockedInEditor").c_str(), EditorStatusTone::Normal);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TEXT::Localize("ui.inspector.unlockHint").c_str());
        ImGui::BeginDisabled();
    }
    DrawTransform(scene, context, *target);

    ImGui::Spacing();
    if (IconTextButton(EditorGlyph::Add, TEXT::Localize("ui.inspector.addComponent").c_str(), "AddComponent",
                       EditorButtonTone::Neutral, ImVec2(-1.0f, 30.0f),
                       TEXT::Localize("ui.inspector.addComponentTooltip").c_str())) {
        openComponentPicker_ = true;
    }

    DrawStatusMessage(commands.GetStatusMessage(), commands.IsStatusError());
    if (!context.componentAddStatusMessage.empty()) {
        DrawStatusMessage(context.componentAddStatusMessage, context.componentAddStatusIsError);
    }

    std::string optionalComponent;
    const auto optionalTypes =
        DocumentComponentAuthoringService{}.GetMissingOptionalComponents(scene.GetComponentRegistry(), *target);
    if (!optionalTypes.empty()) {
        ImGui::SeparatorText(TEXT::Localize("ui.inspector.optionalComponents").c_str());
        ImGui::BeginDisabled(scene.IsRuntimePlayActive());
        for (const auto& type : optionalTypes) {
            const auto* info = scene.GetComponentRegistry().Find(type);
            if (IconTextButton(EditorGlyph::Add, TEXT::Localize(info->presentation.displayName).c_str(), type.c_str(),
                               EditorButtonTone::Quiet, {}, info->presentation.description.c_str()))
                optionalComponent = type;
        }
        ImGui::EndDisabled();
    }
    if (std::optional<SceneObjectAuthoringHistoryRequest> componentHistory =
            componentAuthoringSection_.Draw(scene, context, selectionSync, *target)) {
        historyRequest_ = std::move(componentHistory);
    }
    if (target->editorLocked) {
        ImGui::EndDisabled();
    }
    // 構造変更はパネル描画の後に既存の履歴付きコマンドへ渡す。
    if (!optionalComponent.empty())
        commands.Execute(SceneObjectCommandId::AddComponent, scene, context, selectionSync, optionalComponent);

#else
    (void)scene;
    (void)context;
    (void)selectionSync;
    (void)commands;
#endif
}

void SceneInspectorPanel::RequestRename(GameObject& object) {
    renameObjectId_ = object.GetDocumentId();
    std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", object.GetName().c_str());
    openRenamePopup_ = true;
}

void SceneInspectorPanel::DrawTransform(DocumentSceneBase& scene, EditorContext& context, SceneObjectData& target) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::CollapsingHeader(TEXT::Localize("ui.inspector.transform").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const auto beginHistoryIfActivated = [&]() {
        if (ImGui::IsItemActivated() && !pendingTransformHistory_) {
            pendingTransformHistory_ =
                PendingTransformHistory{scene.GetSceneDocument().objects, scene.GetSceneDocument().camera,
                                        context.sceneDirty || scene.HasUnsavedSceneChanges()};
        }
    };
    const auto applyTransform = [&]() {
        if (context.selection.selectedObject == nullptr) {
            return;
        }
        Transform3D runtime = context.selection.selectedObject->GetTransform();
        runtime.position = target.transform.position;
        runtime.scale = target.transform.scale;
        runtime.rotation = MATH::Quat::FromEulerXYZ(target.transform.rotationEulerDeg.x * kDegreesToRadians,
                                                    target.transform.rotationEulerDeg.y * kDegreesToRadians,
                                                    target.transform.rotationEulerDeg.z * kDegreesToRadians);
        runtime.useExplicitMatrix = false;
        (void)context.selection.selectedObject->SetLocalTransform(runtime);
        context.sceneDirty = true;
    };
    const auto finishHistoryIfDeactivated = [&]() {
        if (!ImGui::IsItemDeactivatedAfterEdit() || !pendingTransformHistory_) {
            return;
        }
        historyRequest_ = SceneObjectAuthoringHistoryRequest{TEXT::Localize("ui.inspector.history.editTransform"),
                                                             std::move(pendingTransformHistory_->beforeObjects),
                                                             scene.GetSceneDocument().objects,
                                                             pendingTransformHistory_->beforeCamera,
                                                             scene.GetSceneDocument().camera,
                                                             pendingTransformHistory_->dirtyBefore};
        pendingTransformHistory_.reset();
    };

    {
        InspectorPropertyRow row(TEXT::Localize("ui.inspector.position").c_str());
        float values[3]{target.transform.position.x, target.transform.position.y, target.transform.position.z};
        const bool changed = row.IsVisible() && ImGui::DragFloat3("##Value", values, 0.05f);
        beginHistoryIfActivated();
        if (changed) {
            target.transform.position = {values[0], values[1], values[2]};
            applyTransform();
        }
        finishHistoryIfDeactivated();
    }
    {
        {
            InspectorPropertyRow row(TEXT::Localize("ui.inspector.rotation").c_str());
            float values[3]{target.transform.rotationEulerDeg.x, target.transform.rotationEulerDeg.y,
                            target.transform.rotationEulerDeg.z};
            const bool changed = row.IsVisible() && ImGui::DragFloat3("##Value", values, 0.5f);
            beginHistoryIfActivated();
            if (changed) {
                target.transform.rotationEulerDeg = {values[0], values[1], values[2]};
                applyTransform();
            }
            finishHistoryIfDeactivated();
        }
    }
    {
        InspectorPropertyRow row(TEXT::Localize("ui.inspector.scale").c_str());
        float values[3]{target.transform.scale.x, target.transform.scale.y, target.transform.scale.z};
        const bool changed = row.IsVisible() && ImGui::DragFloat3("##Value", values, 0.01f);
        beginHistoryIfActivated();
        if (changed) {
            target.transform.scale = {(std::max)(values[0], 0.0001f), (std::max)(values[1], 0.0001f),
                                      (std::max)(values[2], 0.0001f)};
            applyTransform();
        }
        finishHistoryIfDeactivated();
    }
#else
    (void)scene;
    (void)context;
    (void)target;
#endif
}

void SceneInspectorPanel::DrawAddComponentPopup(DocumentSceneBase& scene, EditorContext& context,
                                                const SelectionSyncService& selectionSync,
                                                SceneObjectCommandService& commands) {
#if defined(HIKARI_WITH_EDITOR)
    if (openComponentPicker_) {
        ImGui::OpenPopup("AddComponentPopup");
        openComponentPicker_ = false;
    }
    if (!ImGui::BeginPopup("AddComponentPopup")) {
        return;
    }
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputTextWithHint("##ComponentSearch", TEXT::Localize("ui.inspector.searchComponents").c_str(),
                             componentSearchBuffer_, sizeof(componentSearchBuffer_));
    ImGui::Separator();

    std::vector<const ComponentTypeInfo*> componentTypes = scene.GetComponentRegistry().GetTypeInfos();
    std::sort(componentTypes.begin(), componentTypes.end(), ComponentPresentationLess);
    std::string currentCategory{};
    for (const ComponentTypeInfo* typeInfo : componentTypes) {
        if (typeInfo == nullptr || !MatchesComponent(*typeInfo, componentSearchBuffer_)) {
            continue;
        }
        if (currentCategory != typeInfo->presentation.category) {
            currentCategory = typeInfo->presentation.category;
            const std::string categoryText = LocalizeComponentCategory(currentCategory);
            ImGui::SeparatorText(categoryText.c_str());
        }
        if (ImGui::MenuItem(TEXT::Localize(typeInfo->presentation.displayName).c_str())) {
            commands.Execute(SceneObjectCommandId::AddComponent, scene, context, selectionSync, typeInfo->typeName);
        }
        if (ImGui::IsItemHovered() && !typeInfo->presentation.description.empty()) {
            ImGui::SetTooltip("%s", typeInfo->presentation.description.c_str());
        }
    }
    ImGui::EndPopup();
#else
    (void)scene;
    (void)context;
    (void)selectionSync;
    (void)commands;
#endif
}

std::optional<SceneObjectAuthoringHistoryRequest> SceneInspectorPanel::ConsumeHistoryRequest() {
    std::optional<SceneObjectAuthoringHistoryRequest> request = std::move(historyRequest_);
    historyRequest_.reset();
    return request;
}

std::optional<SceneObjectId> SceneInspectorPanel::ConsumeOpenCinematicsWorkspaceCameraRequest() {
    const std::optional<SceneObjectId> request = openCinematicsWorkspaceCameraRequest_;
    openCinematicsWorkspaceCameraRequest_.reset();
    return request;
}

std::optional<SceneObjectId> SceneInspectorPanel::ConsumeFocusObjectRequest() {
    const std::optional<SceneObjectId> request = focusObjectRequest_;
    focusObjectRequest_.reset();
    return request;
}

} // namespace HIKARI::EDITOR
