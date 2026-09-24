#pragma once

namespace HIKARI {

class DebugCameraController3D;
struct DebugWindowState;
namespace EDITOR {
class EditorCommandRouter;
class EditorOperationRegistry;
class GameExportJob;
class EditorProjectSessionController;
class EditorToolHost;
class EditorWorkspaceHost;
} // namespace EDITOR

class DebugMenuBar {
public:
    void Draw(DebugWindowState& windows, EDITOR::EditorToolHost& toolHost, EDITOR::EditorWorkspaceHost& workspaceHost,
              DebugCameraController3D& debugCamera, bool& environmentLightingEnabled, bool& resetDockingLayoutRequested,
              EDITOR::EditorCommandRouter& commandRouter, EDITOR::EditorProjectSessionController& projectSession,
              bool projectTransitionAllowed, bool hasUnsavedProjectChanges, EDITOR::GameExportJob& exportJob,
              EDITOR::EditorOperationRegistry& operations) const;
};

} // namespace HIKARI
