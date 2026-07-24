#include "HIKARI_DebugMenuBar.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Export/HIKARI_GameExporter.h"
#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"
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
#include <thread>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shobjidl.h>

#include "imgui.h"
#endif

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
    namespace {
        constexpr size_t kExportPathBufferSize = 1024;

        bool g_showGameExportWindow = false;
        EDITOR::GameExportTarget g_exportTarget = EDITOR::GameExportTarget::Game;
        std::array<char, kExportPathBufferSize> g_exportPathBuffer{};
        std::string g_exportStatus{};
        std::vector<EDITOR::GameExportSceneInfo> g_exportScenes{};
        std::vector<std::string> g_selectedSceneGuids{};
        std::string g_startupSceneGuid{};
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

        void SetExportTarget(EDITOR::GameExportTarget target) {
            g_exportTarget = target;
            CopyPathToExportPathBuffer(EDITOR::GameExporter::GetDefaultOutputDirectory(target));
        }

        void OpenGameExportWindow(EDITOR::GameExportTarget target) {
            SetExportTarget(target);
            RefreshExportScenes();
            g_showGameExportWindow = true;
        }

        std::optional<std::filesystem::path> PickFolderWithInitializedCom(
            const std::filesystem::path& initialPath) {
            IFileDialog* dialog = nullptr;
            HRESULT hr = CoCreateInstance(
                CLSID_FileOpenDialog,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&dialog));
            if (FAILED(hr) || dialog == nullptr) {
                return std::nullopt;
            }

            DWORD options = 0;
            if (SUCCEEDED(dialog->GetOptions(&options))) {
                dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            }

            std::filesystem::path initialFolder = initialPath;
            std::error_code ec{};
            if (!initialFolder.empty() && !std::filesystem::exists(initialFolder, ec)) {
                initialFolder = initialFolder.parent_path();
            }
            if (!initialFolder.empty()) {
                IShellItem* folderItem = nullptr;
                hr = SHCreateItemFromParsingName(initialFolder.wstring().c_str(), nullptr, IID_PPV_ARGS(&folderItem));
                if (SUCCEEDED(hr) && folderItem != nullptr) {
                    dialog->SetFolder(folderItem);
                    folderItem->Release();
                }
            }

            std::optional<std::filesystem::path> pickedPath{};
            hr = dialog->Show(nullptr);
            if (SUCCEEDED(hr)) {
                IShellItem* resultItem = nullptr;
                hr = dialog->GetResult(&resultItem);
                if (SUCCEEDED(hr) && resultItem != nullptr) {
                    PWSTR rawPath = nullptr;
                    hr = resultItem->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
                    if (SUCCEEDED(hr) && rawPath != nullptr) {
                        pickedPath = std::filesystem::path(rawPath);
                        CoTaskMemFree(rawPath);
                    }
                    resultItem->Release();
                }
            }

            dialog->Release();
            return pickedPath;
        }

        std::optional<std::filesystem::path> PickFolder(const std::filesystem::path& initialPath) {
            std::optional<std::filesystem::path> pickedPath{};
            std::thread pickerThread([initialPath, &pickedPath]() {
                const HRESULT initResult =
                    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                if (FAILED(initResult)) {
                    return;
                }

                pickedPath = PickFolderWithInitializedCom(initialPath);
                CoUninitialize();
            });
            pickerThread.join();
            return pickedPath;
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
                CopyPathToExportPathBuffer(EDITOR::GameExporter::GetDefaultOutputDirectory(g_exportTarget));
            }

            ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Export Game", &g_showGameExportWindow)) {
                ImGui::TextUnformatted("Target");
                if (ImGui::RadioButton("Game", g_exportTarget == EDITOR::GameExportTarget::Game)) {
                    SetExportTarget(EDITOR::GameExportTarget::Game);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton(
                    "Game + Object Tools",
                    g_exportTarget == EDITOR::GameExportTarget::GameWithObjectTools)) {
                    SetExportTarget(EDITOR::GameExportTarget::GameWithObjectTools);
                }

                ImGui::Spacing();
                ImGui::TextUnformatted("Output Path");
                ImGui::PushItemWidth(-110.0f);
                ImGui::InputText("##GameExportOutputPath", g_exportPathBuffer.data(), g_exportPathBuffer.size());
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Browse...")) {
                    if (const std::optional<std::filesystem::path> pickedPath = PickFolder(ExportPathFromBuffer())) {
                        CopyPathToExportPathBuffer(*pickedPath);
                    }
                }

                if (ImGui::Button("Default Path")) {
                    CopyPathToExportPathBuffer(EDITOR::GameExporter::GetDefaultOutputDirectory(g_exportTarget));
                }

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
                    options.target = g_exportTarget;
                    options.outputDirectory = ExportPathFromBuffer();
                    options.startupSceneGuid = g_startupSceneGuid;
                    options.sceneGuids = SelectedSceneGuidsInDisplayOrder();
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

        if (ImGui::BeginMenu("Workspace")) {
            const bool sceneWorkspaceActive = workspaceHost.IsActive(
                EDITOR::EditorWorkspaceId::Scene);
            if (ImGui::MenuItem("Scene", nullptr, sceneWorkspaceActive)) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId = EDITOR::EditorWorkspaceId::Scene;
                (void)workspaceHost.RequestOpen(std::move(request));
            }

            const bool cinematicsWorkspaceActive = workspaceHost.IsActive(
                EDITOR::EditorWorkspaceId::Cinematics);
            if (ImGui::MenuItem("Cinematics", nullptr, cinematicsWorkspaceActive)) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId = EDITOR::EditorWorkspaceId::Cinematics;
                (void)workspaceHost.RequestOpen(std::move(request));
            }

            const bool modelCollisionWorkspaceActive = workspaceHost.IsActive(
                EDITOR::EditorWorkspaceId::ModelCollision);
            if (ImGui::MenuItem(
                    "Model Collision",
                    nullptr,
                    modelCollisionWorkspaceActive)) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId =
                    EDITOR::EditorWorkspaceId::ModelCollision;
                (void)workspaceHost.RequestOpen(std::move(request));
            }

            const bool animationStateMachineWorkspaceActive =
                workspaceHost.IsActive(
                    EDITOR::EditorWorkspaceId::AnimationStateMachine);
            if (ImGui::MenuItem(
                    "Animation State Machine",
                    nullptr,
                    animationStateMachineWorkspaceActive)) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId =
                    EDITOR::EditorWorkspaceId::AnimationStateMachine;
                (void)workspaceHost.RequestOpen(std::move(request));
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Reset Active Layout")) {
                workspaceHost.RequestResetActiveLayout();
            }
            ImGui::EndMenu();
        }

        EDITOR::DrawEditorDocumentMenu(commandRouter);

        if (ImGui::BeginMenu("Windows")) {
            if (ImGui::BeginMenu("Viewport")) {
                ImGui::MenuItem("Game View", nullptr, &windows.viewport.showGameView);
                ImGui::MenuItem("Viewport HUD", nullptr, &windows.viewport.showViewportHud);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Scene Authoring")) {
                ImGui::MenuItem("Scene Hierarchy", nullptr, &windows.authoring.showSceneWorkspace);
                ImGui::MenuItem("Inspector", nullptr, &windows.authoring.showInspector);
                ImGui::Separator();
                ImGui::MenuItem(
                    "Project Features",
                    nullptr,
                    &windows.authoring.showProjectFeatures);
                ImGui::MenuItem(
                    "Scene Systems",
                    nullptr,
                    &windows.authoring.showSceneSystems);
                if (ImGui::MenuItem("Input Actions...")) {
                    windows.authoring.openInputEditorRequested = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Resources & Environment")) {
                ImGui::MenuItem("Asset Browser", nullptr, &windows.resources.showAssetBrowser);
                ImGui::MenuItem("Environment", nullptr, &windows.resources.showEnvironment);
                ImGui::MenuItem("Quality", nullptr, &windows.resources.showQuality);
                toolHost.DrawMenuItem(EDITOR::kLightingBakeToolId);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Runtime & Debug")) {
                ImGui::MenuItem("Diagnostics", nullptr, &windows.runtime.showDebugWorkspace);
                ImGui::MenuItem("Debug View", nullptr, &windows.runtime.showDebugView);
                ImGui::MenuItem("Performance Audit", nullptr, &windows.runtime.showPerformanceAudit);
                ImGui::MenuItem("Renderer Health", nullptr, &windows.runtime.showValidationLab);
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Reset Docking Layout")) {
                resetDockingLayoutRequested = true;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            toolHost.DrawMenuContents();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("Reset Camera")) {
                const MATH::Vec3 resetPos{ 0.0f, 2.0f, -6.0f };
                debugCamera.Reset(resetPos, 0.0f, 0.0f);
            }
            bool enabled = debugCamera.IsEnabled();
            if (ImGui::MenuItem("Toggle Debug Camera", nullptr, enabled)) {
                debugCamera.SetEnabled(!enabled);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Render")) {
            ImGui::MenuItem("Environment Lighting", nullptr, &environmentLightingEnabled);
            ImGui::Separator();
            if (ImGui::MenuItem("PIX Capture Next Frame", nullptr, false, GFX::PIX::IsCompiledIn())) {
                GFX::PIX::CaptureNextFrames(1, true);
            }
            if (ImGui::MenuItem("Open Last PIX Capture", nullptr, false, GFX::PIX::HasLastCapture())) {
                GFX::PIX::OpenLastCaptureInPix();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Export Game...")) {
                OpenGameExportWindow(EDITOR::GameExportTarget::Game);
            }
            if (ImGui::MenuItem("Export Game With Object Tools...")) {
                OpenGameExportWindow(EDITOR::GameExportTarget::GameWithObjectTools);
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
