#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Documents/HIKARI_DialogueEditorDocument.h"
#include "Editor/Documents/HIKARI_DialogueStyleEditorDocument.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"

namespace HIKARI {
    class AssetDatabase;
    class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

    class DialoguePreviewRenderer;

    enum class DialoguePreviewElement : uint8_t {
        None = 0u,
        Panel,
        SpeakerPlate,
        SpeakerText,
        BodyText,
        ChoicePanel,
        ChoiceText,
        LeftPortrait,
        RightPortrait,
    };

    enum class DialogueWorkspacePage : uint8_t {
        Style = 0u,
        Story = 1u,
    };

    enum class DialogueStyleSection : uint8_t {
        Canvas = 0u,
        Typography,
        Portraits,
        Characters,
        Motion,
    };

    struct DialogueWorkspaceResult {
        bool exitToSceneRequested = false;
        std::string statusMessage{};
    };

    class DialogueWorkspaceController {
    public:
        DialogueWorkspaceController();
        ~DialogueWorkspaceController();

        void ApplyWorkspaceActivation(
            DocumentSceneBase& scene,
            const EditorWorkspaceActivation& activation);
        void DrawDockSpace(bool resetDefaultDockLayout) const;
        DialogueWorkspaceResult Draw(
            DocumentSceneBase& scene,
            EditorCommandRouter& commandRouter);
        void BindDocumentCommands(
            EditorCommandRouter& commandRouter,
            DocumentSceneBase& scene);

        bool Save(DocumentSceneBase& scene, std::string& outMessage);
        bool Undo(std::string& outMessage);
        bool Redo(std::string& outMessage);
        bool NewAsset(std::string& outMessage);
        bool OpenAsset(
            AssetDatabase& database,
            const AssetGuid& assetGuid,
            std::string& outMessage);
        bool IsDocumentOpen() const noexcept;
        bool IsDocumentDirty() const noexcept;

        const DIALOGUE::DialogueDefinition& GetDefinition() const noexcept;
        const DIALOGUE::DialogueProjectStyle& GetStyle() const noexcept;
        const std::string& GetStatusMessage() const noexcept;

    private:
        friend void DrawDialogueWorkspaceWindows(
            DialogueWorkspaceController& controller,
            DocumentSceneBase& scene,
            DialogueWorkspaceResult& result,
            EditorCommandRouter& commandRouter);

        DIALOGUE::DialogueNode* SelectedNode() noexcept;
        const DIALOGUE::DialogueNode* SelectedNode() const noexcept;
        void NormalizeSelection() noexcept;
        void RecordMutation(
            DIALOGUE::DialogueDefinition before,
            uint64_t mergeGroup = 0u);
        void RecordStyleMutation(
            DIALOGUE::DialogueProjectStyle before,
            uint64_t mergeGroup = 0u);
        void NormalizeSpeakerSelection() noexcept;
        std::string AllocateNodeId(std::string_view prefix) const;
        std::string AllocateSpeakerId() const;
        void AddNode(DIALOGUE::DialogueNodeType type);
        void AddSpeaker();
        bool DeleteSelectedSpeaker(std::string& outMessage);
        bool DeleteSelectedNode(std::string& outMessage);
        bool RenameSelectedNode(
            std::string newId,
            std::string& outMessage);

        DialogueEditorDocument document_{};
        DialogueStyleEditorDocument styleDocument_{};
        DialogueWorkspacePage activePage_ = DialogueWorkspacePage::Style;
        DialogueStyleSection styleSection_ = DialogueStyleSection::Canvas;
        std::string selectedNodeId_{};
        std::string selectedSpeakerId_{};
        std::optional<size_t> selectedChoiceIndex_{};
        std::string editingLocale_{ "ja-JP" };
        std::string nodeIdDraft_{};
        std::string nodeIdDraftOwner_{};
        std::string statusMessage_{};
        std::filesystem::path projectRoot_{};
        std::unique_ptr<DialoguePreviewRenderer> previewRenderer_{};
        DialoguePreviewElement previewElement_ =
            DialoguePreviewElement::None;
        std::optional<DIALOGUE::DialogueProjectStyle>
            previewStyleDragBefore_{};
        float previewDragMouseX_ = 0.0f;
        float previewDragMouseY_ = 0.0f;
        bool previewDragResize_ = false;
        bool previewDragChanged_ = false;
        bool previewPlaying_ = true;
        float previewElapsed_ = 1.0f;
        float previewRevealAmount_ = 1.0f;
    };

    void DrawDialogueWorkspaceWindows(
        DialogueWorkspaceController& controller,
        DocumentSceneBase& scene,
        DialogueWorkspaceResult& result,
        EditorCommandRouter& commandRouter);

} // namespace HIKARI::EDITOR
