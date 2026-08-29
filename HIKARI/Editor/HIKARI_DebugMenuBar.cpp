#include "HIKARI_DebugMenuBar.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Export/HIKARI_GameExporter.h"
#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"
#include "Editor/Platform/HIKARI_EditorFileDialog.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Editor/Tools/HIKARI_BuiltInEditorTools.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"

#if defined(HIKARI_WITH_EDITOR)
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#endif

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
    namespace {
        constexpr size_t kExportPathBufferSize = 1024;

        bool g_showGameExportWindow = false;
        std::array<char, kExportPathBufferSize> g_exportPathBuffer{};
        std::string g_exportStatus{};
        std::vector<EDITOR::GameExportSceneInfo> g_exportScenes{};
        std::vector<std::string> g_selectedSceneGuids{};
        std::string g_startupSceneGuid{};
        EDITOR::GameExportWindowMode g_exportWindowMode =
            EDITOR::GameExportWindowMode::BorderlessFullscreen;
        bool g_exportScenesLoaded = false;

        void CopyTextToExportPathBuffer(const std::string& text) {
            g_exportPathBuffer.fill('\0');
            const size_t copyCount = (std::min)(text.size(), g_exportPathBuffer.size() - 1);
            if (copyCount > 0) {
                std::memcpy(g_exportPathBuffer.data(), text.data(), copyCount);
            }
        }

        void CopyPathToExportPathBuffer(const std::filesystem::path& path) {
            CopyTextToExportPathBuffer(path.string());
        }

        std::filesystem::path ExportPathFromBuffer() {
            return std::filesystem::path(g_exportPathBuffer.data());
        }

        bool ContainsGuid(const std::vector<std::string>& guids, const std::string& guid) {
            return std::find(guids.begin(), guids.end(), guid) != guids.end();
        }

        const EDITOR::GameExportSceneInfo* FindSceneInfo(const std::string& guid) {
            const auto it = std::find_if(
                g_exportScenes.begin(),
                g_exportScenes.end(),
                [&guid](const EDITOR::GameExportSceneInfo& scene) {
                    return scene.guid == guid;
                });
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
                g_selectedSceneGuids.erase(
                    std::remove(g_selectedSceneGuids.begin(), g_selectedSceneGuids.end(), guid),
                    g_selectedSceneGuids.end());
                if (g_startupSceneGuid == guid) {
                    g_startupSceneGuid.clear();
                }
            }

            EnsureStartupSceneSelection();
        }

        void DrawSceneExportControls() {
            EnsureExportScenesLoaded();

            ImGui::SeparatorText("Scenes");
            if (ImGui::Button("Refresh Scenes")) {
                RefreshExportScenes();
            }
            ImGui::SameLine();
            if (ImGui::Button("Select All")) {
                SelectAllExportScenes();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                g_selectedSceneGuids.clear();
                g_startupSceneGuid.clear();
            }

            if (g_exportScenes.empty()) {
                ImGui::TextDisabled("No scene assets found.");
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
            if (ImGui::BeginCombo("Startup Scene", startupPreview.c_str())) {
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
                        ImGui::TextDisabled("Startup");
                    } else if (scene.projectStartup) {
                        ImGui::TextDisabled("Project Startup");
                    } else {
                        ImGui::TextDisabled("%s", scene.sourcePath.generic_string().c_str());
                    }
                }
            }
            ImGui::EndChild();

            ImGui::TextDisabled(
                "%d / %d scene(s) selected",
                static_cast<int>(g_selectedSceneGuids.size()),
                static_cast<int>(g_exportScenes.size()));
        }

        void DrawGameExportWindow() {
            if (!g_showGameExportWindow) {
                return;
            }

            if (g_exportPathBuffer[0] == '\0') {
                CopyPathToExportPathBuffer(EDITOR::GameExporter::GetDefaultOutputDirectory());
            }

            ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Export Game", &g_showGameExportWindow)) {
                ImGui::TextUnformatted("Output Path");
                ImGui::PushItemWidth(-110.0f);
                ImGui::InputText("##GameExportOutputPath", g_exportPathBuffer.data(), g_exportPathBuffer.size());
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Browse...")) {
                    if (const std::optional<std::filesystem::path> pickedPath =
                            EDITOR::FILE_DIALOG::PickFolder(
                                ExportPathFromBuffer())) {
                        CopyPathToExportPathBuffer(*pickedPath);
                    }
                }

                if (ImGui::Button("Default Path")) {
                    CopyPathToExportPathBuffer(EDITOR::GameExporter::GetDefaultOutputDirectory());
                }

                const char* windowModePreview =
                    g_exportWindowMode ==
                            EDITOR::GameExportWindowMode::Windowed
                        ? "Windowed"
                        : g_exportWindowMode ==
                                EDITOR::GameExportWindowMode::TopmostFullscreen
                            ? "Topmost Fullscreen"
                            : "Borderless Fullscreen (Recommended)";
                if (ImGui::BeginCombo(
                        "Default Window Mode",
                        windowModePreview)) {
                    const std::array modes{
                        EDITOR::GameExportWindowMode::BorderlessFullscreen,
                        EDITOR::GameExportWindowMode::Windowed,
                        EDITOR::GameExportWindowMode::TopmostFullscreen,
                    };
                    for (const EDITOR::GameExportWindowMode mode : modes) {
                        const char* label = mode ==
                                EDITOR::GameExportWindowMode::Windowed
                            ? "Windowed"
                            : mode == EDITOR::GameExportWindowMode::
                                    TopmostFullscreen
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
                    "Borderless fullscreen is portable and does not force exclusive display ownership.");

                DrawSceneExportControls();

                const bool canExport =
                    !g_exportScenes.empty() &&
                    !g_selectedSceneGuids.empty() &&
                    !g_startupSceneGuid.empty();
                if (!canExport) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Export")) {
                    EDITOR::GameExportOptions options{};
                    options.outputDirectory = ExportPathFromBuffer();
                    options.startupSceneGuid = g_startupSceneGuid;
                    options.sceneGuids = SelectedSceneGuidsInDisplayOrder();
                    options.windowMode = g_exportWindowMode;
                    const EDITOR::GameExportResult result = EDITOR::GameExporter::Export(options);
                    g_exportStatus = result.message;
                    if (result.success) {
                        CopyPathToExportPathBuffer(result.outputDirectory);
                    }
                }
                if (!canExport) {
                    ImGui::EndDisabled();
                }

                const std::filesystem::path currentOutputPath = ExportPathFromBuffer();
                std::error_code ec{};
                const bool canOpenOutput =
                    !currentOutputPath.empty() && std::filesystem::exists(currentOutputPath, ec) && !ec;
                ImGui::SameLine();
                if (!canOpenOutput) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Open Folder")) {
                    EDITOR::SHELL::OpenFolderInExplorer(
                        currentOutputPath);
                }
                if (!canOpenOutput) {
                    ImGui::EndDisabled();
                }

                if (!g_exportStatus.empty()) {
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", g_exportStatus.c_str());
                }
            }
            ImGui::End();
        }
    }

    void DebugMenuBar::Draw(
        DebugWindowState& windows,
        EDITOR::EditorToolHost& toolHost,
        EDITOR::EditorWorkspaceHost& workspaceHost,
        DebugCameraController3D& debugCamera,
        bool& environmentLightingEnabled,
        bool& resetDockingLayoutRequested,
        EDITOR::EditorCommandRouter& commandRouter) const {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }

        if (ImGui::BeginMenu(
                TEXT::Localize("menu.workspace").c_str())) {
            static constexpr std::array workspaceEntries{
                std::pair{
                    EDITOR::EditorWorkspaceId::Scene,
                    "menu.workspace.scene" },
                std::pair{
                    EDITOR::EditorWorkspaceId::Cinematics,
                    "menu.workspace.cinematics" },
                std::pair{
                    EDITOR::EditorWorkspaceId::ModelCollision,
                    "menu.workspace.modelCollision" },
                std::pair{
                    EDITOR::EditorWorkspaceId::ModelMaterial,
                    "menu.workspace.modelMaterial" },
                std::pair{
                    EDITOR::EditorWorkspaceId::AnimationStateMachine,
                    "menu.workspace.animationStateMachine" },
                std::pair{
                    EDITOR::EditorWorkspaceId::RuntimeBinding,
                    "menu.workspace.runtimeBinding" },
                std::pair{
                    EDITOR::EditorWorkspaceId::Vfx,
                    "menu.workspace.vfx" },
                std::pair{
                    EDITOR::EditorWorkspaceId::Dialogue,
                    "menu.workspace.dialogue" },
                std::pair{
                    EDITOR::EditorWorkspaceId::TerrainSurface,
                    "menu.workspace.terrainSurface" },
            };
            for (const auto& [workspaceId, labelKey] :
                    workspaceEntries) {
                if (!ImGui::MenuItem(
                        TEXT::Localize(labelKey).c_str(),
                        nullptr,
                        workspaceHost.IsActive(workspaceId))) {
                    continue;
                }
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId = workspaceId;
                (void)workspaceHost.RequestOpen(std::move(request));
            }

            ImGui::Separator();
            if (ImGui::MenuItem(
                    TEXT::Localize(
                        "menu.workspace.resetActiveLayout").c_str())) {
                workspaceHost.RequestResetActiveLayout();
            }
            ImGui::EndMenu();
        }

        EDITOR::DrawEditorDocumentMenu(commandRouter);

        if (ImGui::BeginMenu(
                TEXT::Localize("menu.windows").c_str())) {
            if (ImGui::BeginMenu(
                    TEXT::Localize("menu.windows.viewport").c_str())) {
                ImGui::MenuItem(
                    TEXT::Localize("menu.windows.gameView").c_str(),
                    nullptr, &windows.viewport.showGameView);
                ImGui::MenuItem(
                    TEXT::Localize("menu.windows.viewportHud").c_str(),
                    nullptr, &windows.viewport.showViewportHud);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(
                    TEXT::Localize(
                        "menu.windows.sceneAuthoring").c_str())) {
                ImGui::MenuItem(
                    TEXT::Localize(
                        "menu.windows.sceneHierarchy").c_str(),
                    nullptr, &windows.authoring.showSceneWorkspace);
                ImGui::MenuItem(
                    TEXT::Localize("menu.windows.inspector").c_str(),
                    nullptr, &windows.authoring.showInspector);
                ImGui::Separator();
                ImGui::MenuItem(
                    TEXT::Localize(
                        "menu.windows.projectFeatures").c_str(),
                    nullptr,
                    &windows.authoring.showProjectFeatures);
                ImGui::MenuItem(
                    TEXT::Localize(
                        "menu.windows.sceneSystems").c_str(),
                    nullptr,
                    &windows.authoring.showSceneSystems);
                if (ImGui::MenuItem(
                        TEXT::Localize(
                            "menu.windows.inputActions").c_str())) {
                    windows.authoring.openInputEditorRequested = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(
                    TEXT::Localize(
                        "menu.windows.resourcesEnvironment").c_str())) {
                ImGui::MenuItem(
                    TEXT::Localize(
                        "menu.windows.assetBrowser").c_str(),
                    nullptr, &windows.resources.showAssetBrowser);
                ImGui::MenuItem(
                    TEXT::Localize(
                        "menu.windows.environment").c_str(),
                    nullptr, &windows.resources.showEnvironment);
                ImGui::MenuItem(
                    TEXT::Localize("menu.windows.quality").c_str(),
                    nullptr, &windows.resources.showQuality);
                toolHost.DrawMenuItem(EDITOR::kLightingBakeToolId);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(
                    TEXT::Localize(
                        "menu.windows.runtimeDebug").c_str())) {
                ImGui::MenuItem(
                    TEXT::Localize("menu.windows.debugView").c_str(),
                    nullptr, &windows.runtime.showDebugView);
                ImGui::MenuItem("Performance Audit", nullptr, &windows.runtime.showPerformanceAudit);
                ImGui::MenuItem(
                    TEXT::Localize("menu.aiAssistant").c_str(),
                    nullptr, &windows.runtime.showAiAssistant);
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem(
                    TEXT::Localize(
                        "menu.windows.resetDockingLayout").c_str())) {
                resetDockingLayoutRequested = true;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(
                TEXT::Localize("menu.tools").c_str())) {
            toolHost.DrawMenuContents();
            ImGui::Separator();
            ImGui::MenuItem(
                TEXT::Localize("menu.editorSettings").c_str(),
                nullptr, &windows.runtime.showEditorSettings);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(
                TEXT::Localize("menu.scene").c_str())) {
            if (ImGui::MenuItem(
                    TEXT::Localize("menu.scene.resetCamera").c_str())) {
                const MATH::Vec3 resetPos{ 0.0f, 2.0f, -6.0f };
                debugCamera.Reset(resetPos, 0.0f, 0.0f);
            }
            bool enabled = debugCamera.IsEnabled();
            if (ImGui::MenuItem(
                    TEXT::Localize(
                        "menu.scene.toggleDebugCamera").c_str(),
                    nullptr, enabled)) {
                debugCamera.SetEnabled(!enabled);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(
                TEXT::Localize("menu.render").c_str())) {
            ImGui::MenuItem(
                TEXT::Localize(
                    "menu.render.environmentLighting").c_str(),
                nullptr, &environmentLightingEnabled);
            ImGui::Separator();
            if (ImGui::MenuItem(
                    TEXT::Localize(
                        "menu.render.pixCaptureNextFrame").c_str(),
                    nullptr, false, GFX::PIX::IsCompiledIn())) {
                GFX::PIX::CaptureNextFrames(1, true);
            }
            if (ImGui::MenuItem(
                    TEXT::Localize(
                        "menu.render.openLastPixCapture").c_str(),
                    nullptr, false, GFX::PIX::HasLastCapture())) {
                GFX::PIX::OpenLastCaptureInPix();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(
                TEXT::Localize("menu.build").c_str())) {
            if (ImGui::MenuItem("Export Game...")) {
                OpenGameExportWindow();
            }

            if (!g_exportStatus.empty()) {
                ImGui::Separator();
                ImGui::TextWrapped("%s", g_exportStatus.c_str());
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
        DrawGameExportWindow();
    }
#else
    void DebugMenuBar::Draw(
        DebugWindowState&,
        EDITOR::EditorToolHost&,
        EDITOR::EditorWorkspaceHost&,
        DebugCameraController3D&,
        bool&,
        bool&,
        EDITOR::EditorCommandRouter&) const {}
#endif

} // namespace HIKARI
