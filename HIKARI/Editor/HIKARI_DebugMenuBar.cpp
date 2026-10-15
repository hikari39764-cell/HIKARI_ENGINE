#include "Editor/Workspaces/HIKARI_EditorWorkspacePresentation.h"
#include "HIKARI_DebugMenuBar.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Export/HIKARI_GameExporter.h"
#include "Editor/Export/HIKARI_GameExportJob.h"
#include "Editor/Export/HIKARI_GameExportIdentityPanel.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"
#include "Editor/Platform/HIKARI_EditorFileDialog.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Editor/Projects/HIKARI_EditorProjectSession.h"
#include "Editor/Tools/HIKARI_BuiltInEditorTools.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

namespace HIKARI {

namespace {
constexpr size_t kExportPathBufferSize = 1024;

bool g_showGameExportWindow = false;
EDITOR::GameExportIdentityPanel g_exportIdentity;
std::array<char, kExportPathBufferSize> g_exportPathBuffer{};
std::string g_exportStatus{};
std::vector<EDITOR::GameExportSceneInfo> g_exportScenes{};
std::vector<std::string> g_selectedSceneGuids{};
std::string g_startupSceneGuid{};
EDITOR::GameExportWindowMode g_exportWindowMode = EDITOR::GameExportWindowMode::BorderlessFullscreen;
bool g_exportScenesLoaded = false;

void CopyTextToExportPathBuffer(const std::string& text) {
    g_exportPathBuffer.fill('\0');
    const size_t copyCount = (std::min)(text.size(), g_exportPathBuffer.size() - 1);
    if (copyCount > 0) {
        std::memcpy(g_exportPathBuffer.data(), text.data(), copyCount);
    }
}

void CopyPathToExportPathBuffer(const std::filesystem::path& path) {
    CopyTextToExportPathBuffer(TEXT::PathToUtf8(path));
}

std::filesystem::path ExportPathFromBuffer() { return TEXT::PathFromUtf8(g_exportPathBuffer.data()); }

bool ContainsGuid(const std::vector<std::string>& guids, const std::string& guid) {
    return std::find(guids.begin(), guids.end(), guid) != guids.end();
}

const EDITOR::GameExportSceneInfo* FindSceneInfo(const std::string& guid) {
    const auto it = std::find_if(g_exportScenes.begin(), g_exportScenes.end(),
                                 [&guid](const EDITOR::GameExportSceneInfo& scene) { return scene.guid == guid; });
    return it == g_exportScenes.end() ? nullptr : &*it;
}

void EnsureStartupSceneSelection() {
    if (!g_startupSceneGuid.empty() && ContainsGuid(g_selectedSceneGuids, g_startupSceneGuid)) {
        return;
    }

    g_startupSceneGuid.clear();
    for (const EDITOR::GameExportSceneInfo& scene : g_exportScenes) {
        if (scene.projectStartup && ContainsGuid(g_selectedSceneGuids, scene.guid)) {
            g_startupSceneGuid = scene.guid;
            return;
        }
    }

    if (!g_selectedSceneGuids.empty()) {
        g_startupSceneGuid = g_selectedSceneGuids.front();
    }
}

void SelectAllExportScenes() {
    g_selectedSceneGuids.clear();
    g_selectedSceneGuids.reserve(g_exportScenes.size());
    for (const EDITOR::GameExportSceneInfo& scene : g_exportScenes) {
        if (!scene.guid.empty()) {
            g_selectedSceneGuids.push_back(scene.guid);
        }
    }
    EnsureStartupSceneSelection();
}

void RefreshExportScenes() {
    const std::vector<std::string> previousSelection = g_selectedSceneGuids;
    const std::string previousStartup = g_startupSceneGuid;

    g_exportScenes = EDITOR::GameExporter::CollectSceneAssets();
    g_exportScenesLoaded = true;

    g_selectedSceneGuids.clear();
    for (const EDITOR::GameExportSceneInfo& scene : g_exportScenes) {
        if (ContainsGuid(previousSelection, scene.guid)) {
            g_selectedSceneGuids.push_back(scene.guid);
        }
    }

    if (g_selectedSceneGuids.empty()) {
        SelectAllExportScenes();
    }

    if (!previousStartup.empty() && ContainsGuid(g_selectedSceneGuids, previousStartup)) {
        g_startupSceneGuid = previousStartup;
    } else {
        g_startupSceneGuid.clear();
        EnsureStartupSceneSelection();
    }
}

void EnsureExportScenesLoaded() {
    if (!g_exportScenesLoaded) {
        RefreshExportScenes();
    }
}

void OpenGameExportWindow() {
    g_exportIdentity.Reset();
    CopyPathToExportPathBuffer(EDITOR::GameExporter::GetDefaultOutputDirectory());
    RefreshExportScenes();
    g_showGameExportWindow = true;
}

std::vector<std::string> SelectedSceneGuidsInDisplayOrder() {
    std::vector<std::string> selected{};
    selected.reserve(g_selectedSceneGuids.size());
    for (const EDITOR::GameExportSceneInfo& scene : g_exportScenes) {
        if (ContainsGuid(g_selectedSceneGuids, scene.guid)) {
            selected.push_back(scene.guid);
        }
    }
    return selected;
}

void SetSceneSelected(const std::string& guid, bool selected) {
    if (guid.empty()) {
        return;
    }

    const bool wasSelected = ContainsGuid(g_selectedSceneGuids, guid);
    if (selected && !wasSelected) {
        g_selectedSceneGuids.push_back(guid);
        if (g_startupSceneGuid.empty()) {
            g_startupSceneGuid = guid;
        }
    } else if (!selected && wasSelected) {
        g_selectedSceneGuids.erase(std::remove(g_selectedSceneGuids.begin(), g_selectedSceneGuids.end(), guid),
                                   g_selectedSceneGuids.end());
        if (g_startupSceneGuid == guid) {
            g_startupSceneGuid.clear();
        }
    }

    EnsureStartupSceneSelection();
}

void DrawSceneExportControls() {
    EnsureExportScenesLoaded();

    ImGui::SeparatorText(TEXT::Localize("ui.resource.scope.scenes").c_str());
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.refresh.scenes.335537", "Refresh Scenes").c_str())) {
        RefreshExportScenes();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.select.all.86a599", "Select All").c_str())) {
        SelectAllExportScenes();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("ui.input.clear", "Clear").c_str())) {
        g_selectedSceneGuids.clear();
        g_startupSceneGuid.clear();
    }

    if (g_exportScenes.empty()) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.no.scene.assets.found.41a36e").c_str());
        return;
    }

    std::string startupPreview = "<none>";
    if (const EDITOR::GameExportSceneInfo* startupScene = FindSceneInfo(g_startupSceneGuid)) {
        startupPreview = startupScene->displayName;
    }

    const bool hasSelection = !g_selectedSceneGuids.empty();
    if (!hasSelection) {
        ImGui::BeginDisabled();
    }
    if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.startup.scene.ee40fc", "Startup Scene").c_str(),
                          startupPreview.c_str())) {
        for (const EDITOR::GameExportSceneInfo& scene : g_exportScenes) {
            if (scene.guid.empty()) {
                continue;
            }

            std::string label = scene.displayName;
            if (scene.projectStartup) {
                label += " (Project Startup)";
            }
            label += "##StartupScene";
            label += scene.guid;

            const bool selected = scene.guid == g_startupSceneGuid;
            if (ImGui::Selectable(label.c_str(), selected)) {
                SetSceneSelected(scene.guid, true);
                g_startupSceneGuid = scene.guid;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (!hasSelection) {
        ImGui::EndDisabled();
    }

    const float sceneListHeight = 150.0f;
    if (ImGui::BeginChild("ExportSceneList", ImVec2(0.0f, sceneListHeight), true)) {
        for (const EDITOR::GameExportSceneInfo& scene : g_exportScenes) {
            if (scene.guid.empty()) {
                continue;
            }

            bool selected = ContainsGuid(g_selectedSceneGuids, scene.guid);
            std::string label = scene.displayName;
            label += "##ExportScene";
            label += scene.guid;
            if (ImGui::Checkbox(label.c_str(), &selected)) {
                SetSceneSelected(scene.guid, selected);
            }

            ImGui::SameLine();
            if (scene.guid == g_startupSceneGuid) {
                ImGui::TextDisabled(TEXT::Localize("editor.ui.startup.79ce33").c_str());
            } else if (scene.projectStartup) {
                ImGui::TextDisabled(TEXT::Localize("editor.ui.project.startup.4394ac").c_str());
            } else {
                ImGui::TextDisabled("%s", scene.sourcePath.generic_string().c_str());
            }
        }
    }
    ImGui::EndChild();

    ImGui::TextDisabled(TEXT::Localize("editor.ui.d.d.scene.s.selected.f815f2").c_str(),
                        static_cast<int>(g_selectedSceneGuids.size()), static_cast<int>(g_exportScenes.size()));
}

void DrawGameExportWindow(EDITOR::GameExportJob& job, EDITOR::EditorOperationRegistry& operations, bool canPrepare) {
    if (!g_showGameExportWindow) {
        return;
    }

    if (g_exportPathBuffer[0] == '\0') {
        CopyPathToExportPathBuffer(EDITOR::GameExporter::GetDefaultOutputDirectory());
    }

    ImGui::SetNextWindowSize(ImVec2(760.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(TEXT::LocalizeWindow("editor.ui.export.game.0d0f28", "Export Game").c_str(),
                     &g_showGameExportWindow)) {
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.shipping.build.asset.validation.cook.package.validation.publish.ffda1a").c_str());
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.uses.saved.project.files.previous.exports.are.preserved.if.prepa.adee39")
                .c_str());
        if (!canPrepare)
            ImGui::TextWrapped(
                TEXT::Localize("editor.ui.stop.play.and.save.project.changes.before.exporting.dfb921").c_str());
        g_exportIdentity.Draw(operations, job.IsRunning());
        ImGui::BeginDisabled(job.IsRunning());
        ImGui::TextUnformatted(TEXT::Localize("editor.ui.output.path.b61e71").c_str());
        ImGui::PushItemWidth(-110.0f);
        ImGui::InputText("##GameExportOutputPath", g_exportPathBuffer.data(), g_exportPathBuffer.size());
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.browse.7d7f40", "Browse...").c_str())) {
            if (const std::optional<std::filesystem::path> pickedPath =
                    EDITOR::FILE_DIALOG::PickFolder(ExportPathFromBuffer())) {
                CopyPathToExportPathBuffer(*pickedPath);
            }
        }

        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.default.path.0b7db9", "Default Path").c_str())) {
            CopyPathToExportPathBuffer(EDITOR::GameExporter::GetDefaultOutputDirectory());
        }

        const char* windowModePreview = g_exportWindowMode == EDITOR::GameExportWindowMode::Windowed ? "Windowed"
                                        : g_exportWindowMode == EDITOR::GameExportWindowMode::TopmostFullscreen
                                            ? "Topmost Fullscreen"
                                            : "Borderless Fullscreen (Recommended)";
        if (ImGui::BeginCombo(
                TEXT::LocalizeWindow("editor.ui.default.window.mode.a119eb", "Default Window Mode").c_str(),
                windowModePreview)) {
            const std::array modes{
                EDITOR::GameExportWindowMode::BorderlessFullscreen,
                EDITOR::GameExportWindowMode::Windowed,
                EDITOR::GameExportWindowMode::TopmostFullscreen,
            };
            for (const EDITOR::GameExportWindowMode mode : modes) {
                const char* label = mode == EDITOR::GameExportWindowMode::Windowed ? "Windowed"
                                    : mode == EDITOR::GameExportWindowMode::TopmostFullscreen
                                        ? "Topmost Fullscreen"
                                        : "Borderless Fullscreen (Recommended)";
                const bool selected = g_exportWindowMode == mode;
                if (ImGui::Selectable(label, selected)) {
                    g_exportWindowMode = mode;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.borderless.fullscreen.is.portable.and.does.not.force.exclusive.d.b61c47")
                .c_str());

        DrawSceneExportControls();

        const bool canExport = canPrepare && g_exportIdentity.IsSaved() && !job.IsRunning() &&
                               !g_exportScenes.empty() && !g_selectedSceneGuids.empty() && !g_startupSceneGuid.empty();
        if (!canExport) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.export.f3e4fa", "Export").c_str())) {
            EDITOR::GameExportOptions options{};
            options.outputDirectory = ExportPathFromBuffer();
            options.startupSceneGuid = g_startupSceneGuid;
            options.sceneGuids = SelectedSceneGuidsInDisplayOrder();
            options.windowMode = g_exportWindowMode;
            const auto result =
                operations.Execute("export.start", {{"outputDirectory", TEXT::PathToUtf8(options.outputDirectory)},
                                                    {"startupSceneGuid", options.startupSceneGuid},
                                                    {"sceneGuids", options.sceneGuids},
                                                    {"windowMode", static_cast<int>(options.windowMode)}});
            g_exportStatus = result.message;
        }
        if (!canExport) {
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();
        if (job.IsRunning()) {
            ImGui::SameLine();
            if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str()))
                (void)operations.Execute("export.cancel");
        }

        const std::filesystem::path currentOutputPath = ExportPathFromBuffer();
        std::error_code ec{};
        const bool canOpenOutput = !currentOutputPath.empty() && std::filesystem::exists(currentOutputPath, ec) && !ec;
        ImGui::SameLine();
        if (!canOpenOutput) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(TEXT::LocalizeWindow("ui.assetInspector.openFolder", "Open Folder").c_str())) {
            EDITOR::SHELL::OpenFolderInExplorer(currentOutputPath);
        }
        if (!canOpenOutput) {
            ImGui::EndDisabled();
        }

        const auto& status = job.Status();
        if (!status.value("message", std::string{}).empty()) {
            ImGui::ProgressBar(status.value("progress", 0.0f));
            ImGui::TextWrapped("%s", status.value("message", std::string{}).c_str());
            if (status.contains("logDirectory") &&
                ImGui::Button(TEXT::LocalizeWindow("editor.ui.open.build.logs.d55e51", "Open Build Logs").c_str()))
                EDITOR::SHELL::OpenFolderInExplorer(TEXT::PathFromUtf8(status.at("logDirectory").get<std::string>()));
        }
        if (!g_exportStatus.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", g_exportStatus.c_str());
        }
    }
    ImGui::End();
}
} // namespace

void DebugMenuBar::Draw(DebugWindowState& windows, EDITOR::EditorToolHost& toolHost,
                        EDITOR::EditorWorkspaceHost& workspaceHost, DebugCameraController3D& debugCamera,
                        bool& environmentLightingEnabled, bool& resetDockingLayoutRequested,
                        EDITOR::EditorCommandRouter& commandRouter,
                        EDITOR::EditorProjectSessionController& projectSession, bool projectTransitionAllowed,
                        bool hasUnsavedProjectChanges, EDITOR::GameExportJob& exportJob,
                        EDITOR::EditorOperationRegistry& operations) const {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    projectSession.DrawMainMenu(projectTransitionAllowed, hasUnsavedProjectChanges);

    if (ImGui::BeginMenu(TEXT::Localize("menu.workspace").c_str())) {
        for (const auto& workspace : EDITOR::kEditorWorkspacePresentations) {
            const auto workspaceId = workspace.id;
            const auto* labelKey = workspace.nameKey;
            if (!ImGui::MenuItem(TEXT::Localize(labelKey).c_str(), nullptr, workspaceHost.IsActive(workspaceId))) {
                continue;
            }
            EDITOR::EditorWorkspaceOpenRequest request{};
            request.workspaceId = workspaceId;
            (void)workspaceHost.RequestOpen(std::move(request));
        }

        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::Localize("menu.workspace.resetActiveLayout").c_str())) {
            workspaceHost.RequestResetActiveLayout();
        }
        ImGui::EndMenu();
    }

    EDITOR::DrawEditorDocumentMenu(commandRouter);

    if (ImGui::BeginMenu(TEXT::Localize("menu.windows").c_str())) {
        if (ImGui::BeginMenu(TEXT::Localize("menu.windows.viewport").c_str())) {
            ImGui::MenuItem(TEXT::Localize("menu.windows.gameView").c_str(), nullptr, &windows.viewport.showGameView);
            ImGui::MenuItem(TEXT::Localize("menu.windows.viewportHud").c_str(), nullptr,
                            &windows.viewport.showViewportHud);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TEXT::Localize("menu.windows.sceneAuthoring").c_str())) {
            ImGui::MenuItem(TEXT::Localize("menu.windows.sceneHierarchy").c_str(), nullptr,
                            &windows.authoring.showSceneWorkspace);
            ImGui::MenuItem(TEXT::Localize("menu.windows.inspector").c_str(), nullptr,
                            &windows.authoring.showInspector);
            ImGui::Separator();
            ImGui::MenuItem(TEXT::Localize("menu.windows.projectFeatures").c_str(), nullptr,
                            &windows.authoring.showProjectFeatures);
            ImGui::MenuItem(TEXT::Localize("menu.windows.sceneSystems").c_str(), nullptr,
                            &windows.authoring.showSceneSystems);
            if (ImGui::MenuItem(TEXT::Localize("menu.windows.inputActions").c_str())) {
                windows.authoring.openInputEditorRequested = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TEXT::Localize("menu.windows.resourcesEnvironment").c_str())) {
            ImGui::MenuItem(TEXT::Localize("menu.windows.assetBrowser").c_str(), nullptr,
                            &windows.resources.showAssetBrowser);
            ImGui::MenuItem(TEXT::Localize("menu.windows.environment").c_str(), nullptr,
                            &windows.resources.showEnvironment);
            ImGui::MenuItem(TEXT::Localize("menu.windows.quality").c_str(), nullptr, &windows.resources.showQuality);
            toolHost.DrawMenuItem(EDITOR::kLightingBakeToolId);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TEXT::Localize("menu.windows.runtimeDebug").c_str())) {
            ImGui::MenuItem(TEXT::Localize("menu.windows.debugView").c_str(), nullptr, &windows.runtime.showDebugView);
            ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.performance.audit.e54bcf", "Performance Audit").c_str(),
                            nullptr, &windows.runtime.showPerformanceAudit);
            ImGui::MenuItem(TEXT::Localize("menu.aiAssistant").c_str(), nullptr, &windows.runtime.showAiAssistant);
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::Localize("menu.windows.resetDockingLayout").c_str())) {
            resetDockingLayoutRequested = true;
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TEXT::Localize("menu.tools").c_str())) {
        toolHost.DrawMenuContents();
        ImGui::Separator();
        ImGui::MenuItem(TEXT::Localize("menu.editorSettings").c_str(), nullptr, &windows.runtime.showEditorSettings);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TEXT::Localize("menu.scene").c_str())) {
        if (ImGui::MenuItem(TEXT::Localize("menu.scene.resetCamera").c_str())) {
            const MATH::Vec3 resetPos{0.0f, 2.0f, -6.0f};
            debugCamera.Reset(resetPos, 0.0f, 0.0f);
        }
        bool enabled = debugCamera.IsEnabled();
        if (ImGui::MenuItem(TEXT::Localize("menu.scene.toggleDebugCamera").c_str(), nullptr, enabled)) {
            debugCamera.SetEnabled(!enabled);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TEXT::Localize("menu.render").c_str())) {
        ImGui::MenuItem(TEXT::Localize("menu.render.environmentLighting").c_str(), nullptr,
                        &environmentLightingEnabled);
        ImGui::Separator();
        if (ImGui::MenuItem(TEXT::Localize("menu.render.pixCaptureNextFrame").c_str(), nullptr, false,
                            GFX::PIX::IsCompiledIn())) {
            GFX::PIX::CaptureNextFrames(1, true);
        }
        if (ImGui::MenuItem(TEXT::Localize("menu.render.openLastPixCapture").c_str(), nullptr, false,
                            GFX::PIX::HasLastCapture())) {
            GFX::PIX::OpenLastCaptureInPix();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TEXT::Localize("menu.build").c_str())) {
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.export.game.dfd4f6", "Export Game...").c_str())) {
            OpenGameExportWindow();
        }

        if (!g_exportStatus.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", g_exportStatus.c_str());
        }

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
    DrawGameExportWindow(exportJob, operations, projectTransitionAllowed && !hasUnsavedProjectChanges);
}

} // namespace HIKARI
