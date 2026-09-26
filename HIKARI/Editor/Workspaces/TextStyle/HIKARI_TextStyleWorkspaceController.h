#pragma once
#include <json.hpp>
#include "Editor/Documents/HIKARI_TextStyleEditorDocument.h"
#include "Editor/Views/HIKARI_GameUiPreviewTarget.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"
#include <functional>
namespace HIKARI {
class DocumentSceneBase;
}
namespace HIKARI::EDITOR {
class EditorOperationRegistry;
class EditorCommandRouter;
class EditorWorkspaceHost;
class TextStyleWorkspaceController {
public:
    TextStyleEditorDocument& Document() { return document_; }
    const TextStyleEditorDocument& Document() const { return document_; }
    // 文書の状態。シェル、AI の文脈、バックグラウンドのセッションで共有する。
    nlohmann::json DescribeDocumentState() const;
    void Draw(DocumentSceneBase&, EditorOperationRegistry&, EditorCommandRouter&);
    void DrawDockSpace(bool reset);
    void ApplyActivation(DocumentSceneBase&, const EditorWorkspaceActivation&);
    void Deactivate() {
        target_.Reset();
        content_.Reset();
    }
    void BindDocumentCommands(EditorCommandRouter&);
    const GAME_UI::GameUiFrameSource& BuildPreview(AssetDatabase&);
    const char* PreviewMessageKey() const { return previewMessageKey_; }

private:
    TextStyleEditorDocument document_;
    GameUiPreviewTarget target_;
    GAME_UI::GameUiPresentationService content_;
    std::filesystem::path root_;
    uint64_t frame_ = 0;
    std::string message_;
    const char* previewMessageKey_ = nullptr;
    char search_[128]{};
    char preview_[2048] = "HIKARI / 文字样式 / テキスト\nAdventure begins here. 0123456789";
    bool light_ = false;
    bool pendingSwitch_ = false;
    AssetGuid pendingGuid_;
};
void RegisterTextStyleOperations(EditorOperationRegistry&, TextStyleWorkspaceController&, EditorWorkspaceHost&,
                                 std::function<DocumentSceneBase*()>);
} // namespace HIKARI::EDITOR
