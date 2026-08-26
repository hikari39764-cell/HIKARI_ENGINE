#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"

namespace HIKARI::EDITOR {

    class EditorWorkspaceHost {
    public:
        EditorWorkspaceHost();

        bool RequestOpen(EditorWorkspaceOpenRequest request);
        const EditorWorkspaceOpenRequest* PeekPending() const noexcept;
        void CancelPending() noexcept;
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
        EditorViewInstance& GetModelMaterialPreviewView() noexcept;
        const EditorViewInstance& GetModelMaterialPreviewView() const noexcept;
        EditorViewInstance& GetModelMaterialCompareView() noexcept;
        const EditorViewInstance& GetModelMaterialCompareView() const noexcept;
        EditorViewInstance& GetVfxPreviewView() noexcept;
        const EditorViewInstance& GetVfxPreviewView() const noexcept;
        EditorViewInstance& GetTerrainSurfaceGameView() noexcept;
        const EditorViewInstance& GetTerrainSurfaceGameView() const noexcept;
        EditorViewInstance* FindView(
            RENDER3D::RenderViewId renderViewId) noexcept;
        const EditorViewInstance* FindView(
            RENDER3D::RenderViewId renderViewId) const noexcept;

    private:
        enum class BuiltInViewSlot : size_t {
            CinematicsGame,
            CinematicsDirector,
            CinematicsOverview,
            ModelCollisionPreview,
            ModelMaterialPreview,
            ModelMaterialCompare,
            VfxPreview,
            TerrainSurfaceGame,
            Count,
        };

        EditorViewInstance& View(BuiltInViewSlot slot) noexcept;
        const EditorViewInstance& View(BuiltInViewSlot slot) const noexcept;

        EditorWorkspaceId activeWorkspace_ = EditorWorkspaceId::Scene;
        std::optional<EditorWorkspaceOpenRequest> pendingOpenRequest_{};
        std::array<bool,
            static_cast<size_t>(EditorWorkspaceId::Count)>
            resetLayoutRequests_{};
        std::array<EditorViewInstance,
            static_cast<size_t>(BuiltInViewSlot::Count)> views_{};
    };

} // namespace HIKARI::EDITOR
