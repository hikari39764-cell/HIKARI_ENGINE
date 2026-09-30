#include "Editor/Workspaces/TextStyle/HIKARI_TextStyleWorkspaceController.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "HIKARI/Project/FrameContext.h"
#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }
} // namespace
void TextStyleWorkspaceController::DrawContextTools(DocumentSceneBase& scene, EditorOperationRegistry& operations) {
    auto* selected = scene.GetWorld().FindObject(previewObject_);
    if (!selected || !selected->GetComponent<UiTextComponent>()) {
        selected = nullptr;
        previewObject_ = {};
    }
    ImGui::Checkbox(L("textstyle.contextPreview"), &contextPreview_);
    if (ImGui::BeginCombo(L("textstyle.previewObject"), selected ? selected->GetName().c_str() : L("gc.none"))) {
        if (ImGui::Selectable(L("gc.none"), !previewObject_.value))
            previewObject_ = {};
        for (const auto& object : scene.GetWorld().GetObjects()) {
            if (!object || !object->GetComponent<UiTextComponent>())
                continue;
            ImGui::PushID(static_cast<int>(object->GetDocumentId().value));
            if (ImGui::Selectable(object->GetName().c_str(), previewObject_ == object->GetDocumentId()))
                previewObject_ = object->GetDocumentId();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", L("textstyle.contextHelp"));
    bool editable = false;
    for (const auto& object : scene.GetSceneDocument().objects)
        if (object.id == previewObject_)
            editable = !object.editorLocked;
    ImGui::BeginDisabled(!editable || !document_.IsOpen() || scene.IsRuntimePlayActive());
    if (ImGui::Button(L("textstyle.applyToObject"))) {
        auto result = operations.Execute("text.style.save", {});
        nlohmann::json commands = nlohmann::json::array();
        if (result.ok)
            for (const auto& object : scene.GetSceneDocument().objects)
                if (object.id == previewObject_)
                    for (const auto& component : object.components)
                        if (component.type == "UiTextComponent")
                            commands.push_back({{"objectId", object.id.value},
                                                {"componentId", component.id.value},
                                                {"propertyId", "textStyleAssetGuid"},
                                                {"value", document_.Guid().value}});
        if (result.ok && !commands.empty())
            result = operations.Execute("authoring.transaction", {{"commands", commands}});
        message_ = result.ok ? L("textstyle.appliedHelp") : result.message;
    }
    ImGui::EndDisabled();
    if (document_.Guid().IsValid()) {
        size_t count = 0;
        for (const auto& object : scene.GetSceneDocument().objects)
            for (const auto& component : object.components)
                if (component.properties.value("textStyleAssetGuid", std::string{}) == document_.Guid().value)
                    ++count;
        ImGui::Text(L("textstyle.sceneUses"), static_cast<int>(count));
    }
}
const GAME_UI::GameUiFrameSource& TextStyleWorkspaceController::BuildContextPreview(DocumentSceneBase& scene) {
    auto& database = scene.GetAssetDatabase();
    auto* object = scene.GetWorld().FindObject(previewObject_);
    auto* canvas = object;
    for (int depth = 0; canvas && !canvas->GetComponent<UiCanvasComponent>() && depth < 64; ++depth)
        canvas = canvas->GetParent();
    if (!canvas || !canvas->GetComponent<UiCanvasComponent>())
        return BuildPreview(database);
    if (root_ != database.GetProjectRoot()) {
        root_ = database.GetProjectRoot();
        content_.SetProjectRoot(root_);
    }
    content_.SetAssetDatabase(&database);
    content_.DiscardPending();
    previewMessageKey_ = nullptr;
    projector_.SetPreviewTextStyle(previewObject_, &document_.Profile());
    FrameContext frame{};
    frame.frameIndex = ++frame_;
    projector_.BuildPresentation(scene.GetWorld(), frame, content_, canvas->GetDocumentId());
    projector_.SetPreviewTextStyle({}, nullptr);
    if (projector_.RejectedTextRequests())
        previewMessageKey_ = "textstyle.previewRejected";
    content_.Tick(frame_);
    return content_.GetFrameSource();
}
} // namespace HIKARI::EDITOR
