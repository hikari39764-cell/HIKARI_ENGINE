#pragma once

#include <optional>

#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"

namespace HIKARI::EDITOR {

    class EditorWorkspaceHost {
    public:
        EditorWorkspaceHost();

        bool RequestOpen(EditorWorkspaceOpenRequest request);
        std::optional<EditorWorkspaceActivation> ApplyPending();

        EditorWorkspaceId GetActive() const noexcept;
        bool IsActive(EditorWorkspaceId workspaceId) const noexcept;

        void RequestResetActiveLayout() noexcept;
        bool ConsumeReset(EditorWorkspaceId workspaceId) noexcept;

        EditorViewInstance& GetCinematicsGameView() noexcept;
        const EditorViewInstance& GetCinematicsGameView() const noexcept;
        EditorViewInstance& GetCinematicsDirectorView() noexcept;
        const EditorViewInstance& GetCinematicsDirectorView() const noexcept;
        EditorViewInstance& GetCinematicsOverviewView() noexcept;
        const EditorViewInstance& GetCinematicsOverviewView() const noexcept;
        EditorViewInstance& GetModelCollisionPreviewView() noexcept;
        const EditorViewInstance& GetModelCollisionPreviewView() const noexcept;

    private:
        EditorWorkspaceId activeWorkspace_ = EditorWorkspaceId::Scene;
        std::optional<EditorWorkspaceOpenRequest> pendingOpenRequest_{};
        bool resetSceneLayoutRequested_ = false;
        bool resetCinematicsLayoutRequested_ = false;
        bool resetModelCollisionLayoutRequested_ = false;
        bool resetAnimationStateMachineLayoutRequested_ = false;
        EditorViewInstance cinematicsGameView_{};
        EditorViewInstance cinematicsDirectorView_{};
        EditorViewInstance cinematicsOverviewView_{};
        EditorViewInstance modelCollisionPreviewView_{};
    };

} // namespace HIKARI::EDITOR
