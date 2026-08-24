#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Documents/HIKARI_CombatActionEditorDocument.h"
#include "Editor/Views/Director/HIKARI_DirectorViewPanel.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"

namespace HIKARI {
    class DocumentSceneBase;
    class SelectionSyncService;
    struct EditorContext;

namespace EDITOR {
    class EditorWorkspaceHost;

    enum class CombatActionTrackKind : uint8_t {
        None = 0u,
        Animation,
        Phase,
        Hit,
        Cancel,
        Motion,
        Defense,
        Vfx,
        Presentation,
    };

    struct CombatActionWorkspaceResult {
        bool exitToSceneRequested = false;
        std::optional<AssetGuid> openSequenceAssetGuid{};
        std::string statusMessage{};
    };

    class CombatActionWorkspaceController {
    public:
        CombatActionWorkspaceController() = default;

        void ApplyWorkspaceActivation(
            DocumentSceneBase& scene,
            const EditorWorkspaceActivation& activation);
        void DrawDockSpace(bool resetDefaultDockLayout) const;
        CombatActionWorkspaceResult Draw(
            DocumentSceneBase& scene,
            EditorContext& context,
            SelectionSyncService& selectionSync,
            EditorWorkspaceHost& workspaceHost,
            EditorCommandRouter& commandRouter);
        void BindDocumentCommands(
            EditorCommandRouter& commandRouter,
            DocumentSceneBase& scene);

        bool Save(DocumentSceneBase& scene, std::string& outMessage);
        bool Undo(std::string& outMessage);
        bool Redo(std::string& outMessage);
        bool NewAsset(std::string& outMessage);
        bool OpenAsset(
            DocumentSceneBase& scene,
            const AssetGuid& assetGuid,
            std::string& outMessage);

    private:
        struct ItemSelection {
            CombatActionTrackKind track = CombatActionTrackKind::None;
            uint64_t id = 0u;
        };

        struct AnimatorPreviewSnapshot {
            SceneObjectId objectId{};
            std::string clip{};
            float timeSeconds = 0.0f;
            float speed = 1.0f;
            bool loop = true;
            bool playing = false;
            bool valid = false;
        };

        void DrawNavigator(
            DocumentSceneBase& scene,
            CombatActionWorkspaceResult& result);
        void DrawPreview(
            DocumentSceneBase& scene,
            EditorContext& context,
            SelectionSyncService& selectionSync,
            EditorWorkspaceHost& workspaceHost);
        void DrawTimeline();
        void DrawInspector(DocumentSceneBase& scene);
        void DrawDiagnostics() const;
        void AdvancePreview(float unscaledDeltaTime);
        void ApplyPreviewPose(DocumentSceneBase& scene, EditorContext& context);
        void RestorePreviewPose(DocumentSceneBase& scene);
        void NormalizeSelection() noexcept;
        void DeleteSelection();
        void AddItem(CombatActionTrackKind track);
        void RecordMutation(
            ACTION::CombatActionDefinition before,
            uint64_t mergeGroup = 0u);

        CombatActionEditorDocument document_{};
        DirectorViewPanel directorViewPanel_{};
        ItemSelection selection_{};
        AnimatorPreviewSnapshot previewSnapshot_{};
        SceneObjectId previewTargetObjectId_{};
        float currentTimeSeconds_ = 0.0f;
        float timelinePixelsPerSecond_ = 360.0f;
        float timelineScrollSeconds_ = 0.0f;
        bool playing_ = false;
        bool looping_ = true;
        bool snapEnabled_ = true;
        std::string assetSearch_{};
        std::string statusMessage_{};
        std::optional<ACTION::CombatActionDefinition> timelineDragBefore_{};
        uint8_t timelineDragMode_ = 0u;
        float timelineDragGrabOffsetSeconds_ = 0.0f;
    };

} // namespace EDITOR
} // namespace HIKARI
