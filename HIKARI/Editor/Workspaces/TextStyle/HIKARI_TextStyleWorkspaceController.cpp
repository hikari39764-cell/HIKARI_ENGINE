#include <algorithm>
#include <tuple>
#include "Editor/Workspaces/TextStyle/HIKARI_TextStyleWorkspaceController.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "imgui.h"
#include "imgui_internal.h"
namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }
std::string Window(const char* key, const char* id) { return TEXT::Localize(key) + "###TextStyle/" + id; }
} // namespace
void TextStyleWorkspaceController::DrawDockSpace(bool reset) {
    const auto* viewport = ImGui::GetMainViewport();
    const ImGuiID id = ImGui::GetID("TextStyleDockSpace");
    if (reset || !ImGui::DockBuilderGetNode(id)) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(id, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);
        ImGuiID center = id, left = 0, right = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .22f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, .34f, &right, &center);
        ImGui::DockBuilderDockWindow("###TextStyle/Library", left);
        ImGui::DockBuilderDockWindow("###TextStyle/Preview", center);
        ImGui::DockBuilderDockWindow("###TextStyle/Inspector", right);
        ImGui::DockBuilderFinish(id);
    }
    DrawWorkspaceDockSpace(id, viewport);
}
void TextStyleWorkspaceController::ApplyActivation(DocumentSceneBase& scene,
                                                   const EditorWorkspaceActivation& activation) {
    if (activation.current == EditorWorkspaceId::Text && activation.textStyleAssetGuid) {
        if (document_.IsDirty() && document_.Guid() != *activation.textStyleAssetGuid) {
            pendingGuid_ = *activation.textStyleAssetGuid;
            pendingSwitch_ = true;
        } else
            document_.Open(scene.GetAssetDatabase(), *activation.textStyleAssetGuid, message_);
    }
}
void TextStyleWorkspaceController::BindDocumentCommands(EditorCommandRouter& router) {
    router.BindOperation(EditorCommandId::SaveDocument, L("uix.save"), document_.IsOpen(), "text.style.save");
    router.BindOperation(EditorCommandId::Undo, L("uix.undo"), document_.CanUndo(), "text.style.undo");
    router.BindOperation(EditorCommandId::Redo, L("uix.redo"), document_.CanRedo(), "text.style.redo");
}
const GAME_UI::GameUiFrameSource& TextStyleWorkspaceController::BuildPreview(AssetDatabase& database) {
    if (root_ != database.GetProjectRoot()) {
        root_ = database.GetProjectRoot();
        content_.SetProjectRoot(root_);
    }
    content_.SetAssetDatabase(&database);
    content_.DiscardPending();
    previewMessageKey_ = nullptr;
    if (document_.IsOpen()) {
        const auto& profile = document_.Profile();
        TextAssetStore store;
        store.Configure(&database);
        const auto font = store.ResolveFont(profile.font);
        GameProjectUiTextRequest request{};
        // 専用 presentation 内でも、描画要求には有効な所有者 ID が必要。
        request.sourceId = 1;
        request.referenceWidth = 960;
        request.referenceHeight = 540;
        request.x = 32;
        request.y = 32;
        request.maxWidth = 896;
        request.maxHeight = 476;
        request.pixelHeight = profile.pixelHeight;
        request.textColor = profile.color;
        profile.effects.Apply(request);
        if (font.empty())
            previewMessageKey_ = "uix.fontMissing";
        else if (preview_[0] == '\0')
            previewMessageKey_ = "textstyle.emptyPreview";
        else if (!content_.SubmitStyledText(font, preview_, request, profile.effects.glow))
            previewMessageKey_ = "textstyle.previewRejected";
    }
    content_.Tick(++frame_);
    return content_.GetFrameSource();
}
void TextStyleWorkspaceController::Draw(DocumentSceneBase& scene, EditorOperationRegistry& operations,
                                        EditorCommandRouter& commands) {
    auto execute = [&](const char* id, nlohmann::json values = nlohmann::json::object()) {
        const auto result = operations.Execute(id, values);
        message_ = result.ok ? std::string{} : result.message;
    };
    auto& database = scene.GetAssetDatabase();
    auto requestSwitch = [&](AssetGuid guid) {
        if (document_.IsDirty()) {
            pendingGuid_ = std::move(guid);
            pendingSwitch_ = true;
        } else if (guid.IsValid())
            execute("text.style.open", {{"assetGuid", guid.value}});
        else
            execute("text.style.new");
    };
    const auto popup = Window("textstyle.unsaved", "Unsaved");
    if (pendingSwitch_)
        ImGui::OpenPopup(popup.c_str());
    if (ImGui::BeginPopupModal(popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(L("textstyle.unsavedHelp"));
        const auto perform = [&] {
            if (pendingGuid_.IsValid())
                execute("text.style.open", {{"assetGuid", pendingGuid_.value}, {"discard", true}});
            else
                execute("text.style.new", {{"discard", true}});
            pendingSwitch_ = false;
            ImGui::CloseCurrentPopup();
        };
        if (ImGui::Button(L("textstyle.saveContinue"))) {
            execute("text.style.save");
            if (message_.empty())
                perform();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("textstyle.discardContinue")))
            perform();
        ImGui::SameLine();
        if (ImGui::Button(L("ui.common.cancel"))) {
            pendingSwitch_ = false;
            ImGui::CloseCurrentPopup();
        }
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
        ImGui::EndPopup();
    }
    if (ImGui::Begin(Window("textstyle.library", "Library").c_str())) {
        if (IconTextButton(EditorGlyph::Add, L("textstyle.new"), "NewStyle"))
            requestSwitch({});
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Search", L("uix.search"), search_, sizeof(search_));
        const auto search = TEXT::ToLowerAsciiCopy(search_);
        for (const auto* record : database.CollectByType(AssetType::TextStyle)) {
            if (!search.empty() && TEXT::ToLowerAsciiCopy(record->displayName).find(search) == std::string::npos)
                continue;
            ImGui::PushID(record->guid.value.c_str());
            if (ImGui::Selectable(record->displayName.c_str(), document_.Guid() == record->guid))
                requestSwitch(record->guid);
            ImGui::PopID();
        }
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", L("textstyle.workflow"));
    }
    ImGui::End();
    if (ImGui::Begin(Window("textstyle.preview", "Preview").c_str())) {
        for (const auto [id, glyph, label] : {std::tuple{EditorCommandId::SaveDocument, EditorGlyph::Save, "uix.save"},
                                              std::tuple{EditorCommandId::Undo, EditorGlyph::Undo, "uix.undo"},
                                              std::tuple{EditorCommandId::Redo, EditorGlyph::Redo, "uix.redo"}}) {
            ImGui::BeginDisabled(!commands.CanExecute(id));
            if (IconButton(glyph, label, EditorButtonTone::Quiet, {28, 28}, L(label)))
                commands.Execute(id);
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(!document_.IsOpen());
        if (ImGui::Button(L("textstyle.saveCopy")))
            execute("text.style.save", {{"saveAs", true}});
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox(L("textstyle.light"), &light_);
        if (document_.IsOpen()) {
            ImGui::Text("%s%s", document_.Profile().name.c_str(), document_.IsDirty() ? " *" : "");
            ImGui::InputTextMultiline("##Sample", preview_, sizeof(preview_), {-1, 60});
            const auto& source = BuildPreview(database);
            if (previewMessageKey_)
                ImGui::TextWrapped("%s", L(previewMessageKey_));
            const auto available = ImGui::GetContentRegionAvail();
            const float width = std::max(64.f, std::min(available.x, available.y * 16.f / 9.f));
            const auto srv = target_.Render(source, 960, 540,
                                            light_ ? GameProjectUiColor{.88f, .9f, .93f, 1}
                                                   : GameProjectUiColor{.035f, .045f, .065f, 1});
            if (!srv.ptr)
                ImGui::TextWrapped("%s", L("uix.previewFailed"));
            else if (target_.FailedTextRequests())
                ImGui::TextWrapped("%s", L("uix.previewFontFailed"));
            if (srv.ptr)
                ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr)),
                             {width, width * 9.f / 16.f});
        } else
            ImGui::TextWrapped("%s", L("textstyle.start"));
    }
    ImGui::End();
    if (ImGui::Begin(Window("uix.inspector", "Inspector").c_str()) && document_.IsOpen()) {
        auto draft = document_.Profile();
        ImGuiInspectorBuilder inspector;
        InspectorContext context{};
        context.assetDatabase = &database;
        inspector.SetContext(context);
        InspectTextStyleAsset(inspector, draft);
        if (WriteTextStyleAsset(draft) != WriteTextStyleAsset(document_.Profile()))
            execute("text.style.edit", {{"asset", WriteTextStyleAsset(draft)}, {"mergeGroup", ImGui::GetActiveID()}});
        if (!ImGui::IsAnyItemActive())
            document_.SealMerge();
        ImGui::Separator();
        ImGui::TextWrapped("%s", L("textstyle.worldSupport"));
    }
    ImGui::End();
}
nlohmann::json TextStyleWorkspaceController::DescribeDocumentState() const {
    return {{"open", document_.IsOpen()}, {"assetGuid", document_.Guid().value}, {"dirty", document_.IsDirty()}};
}

} // namespace HIKARI::EDITOR
