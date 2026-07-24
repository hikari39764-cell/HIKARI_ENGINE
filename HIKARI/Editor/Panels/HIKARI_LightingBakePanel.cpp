#include "HIKARI_LightingBakePanel.h"

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Assets/Lighting/HIKARI_LightProbeVolumeFormat.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

#include <algorithm>

namespace HIKARI {

    namespace {

        TOOLS::BAKING::LightingBakeRequest BuildLightingBakeRequest(DocumentSceneBase& scene) {
            TOOLS::BAKING::LightingBakeRequest request{};
            request.projectRoot = scene.GetAssetDatabase().GetProjectRoot();
            request.sceneGuid = scene.GetCurrentSceneAssetGuid().value;
            request.sceneName = scene.GetCurrentSceneDisplayName();
            request.scenePath = scene.GetScenePath();
            request.sceneDocument = &scene.GetSceneDocument();
            request.target = TOOLS::BAKING::LightingBakeTarget::All;
            return request;
        }

        const std::string& LastText(const std::vector<std::string>& values) {
            static const std::string empty{};
            return values.empty() ? empty : values.back();
        }

#if defined(HIKARI_WITH_EDITOR)
        void DrawReportLines(const char* label, const std::vector<std::string>& lines, const ImVec4& color) {
            if (lines.empty()) {
                return;
            }

            ImGui::TextUnformatted(label);
            ImGui::Indent();
            for (const std::string& line : lines) {
                ImGui::TextColored(color, "%s", line.c_str());
            }
            ImGui::Unindent();
        }

        int CaptureResolutionToIndex(uint32_t resolution) {
            const uint32_t normalized = NormalizeLightProbeCaptureResolution(resolution);
            if (normalized == 16u) {
                return 0;
            }
            if (normalized == 32u) {
                return 1;
            }
            return 2;
        }

        uint32_t CaptureResolutionFromIndex(int index) {
            static constexpr uint32_t kResolutions[] = { 16u, 32u, 64u };
            return kResolutions[std::clamp(index, 0, 2)];
        }

        void DrawLightProbeVolumeSettings(
            DocumentSceneBase& scene,
            const std::filesystem::path& volumePath) {

            SceneDocument& document = scene.GetSceneDocument();
            LightProbeVolumeSettings& settings = document.lightingBake.lightProbeVolume;
            ClampLightProbeVolumeSettings(settings);

            ImGui::Spacing();
            ImGui::TextUnformatted("Light Probe Volume");
            ImGui::Separator();

            bool changed = false;
            bool runtimeChanged = false;

            bool enabled = settings.enabled;
            if (ImGui::Checkbox("Enabled##LightProbeVolume", &enabled)) {
                settings.enabled = enabled;
                changed = true;
                runtimeChanged = true;
            }

            float origin[3] = { settings.origin.x, settings.origin.y, settings.origin.z };
            if (ImGui::DragFloat3("Origin##LightProbeVolume", origin, 0.05f)) {
                settings.origin = { origin[0], origin[1], origin[2] };
                changed = true;
            }

            float size[3] = { settings.size.x, settings.size.y, settings.size.z };
            if (ImGui::DragFloat3("Size##LightProbeVolume", size, 0.05f, 0.1f, 100.0f)) {
                settings.size = { size[0], size[1], size[2] };
                changed = true;
            }

            int countX = static_cast<int>(settings.countX);
            int countY = static_cast<int>(settings.countY);
            int countZ = static_cast<int>(settings.countZ);
            if (ImGui::InputInt("Count X##LightProbeVolume", &countX)) {
                settings.countX = static_cast<uint32_t>((std::max)(0, countX));
                changed = true;
            }
            if (ImGui::InputInt("Count Y##LightProbeVolume", &countY)) {
                settings.countY = static_cast<uint32_t>((std::max)(0, countY));
                changed = true;
            }
            if (ImGui::InputInt("Count Z##LightProbeVolume", &countZ)) {
                settings.countZ = static_cast<uint32_t>((std::max)(0, countZ));
                changed = true;
            }

            const char* resolutionLabels[] = { "16", "32", "64" };
            int resolutionIndex = CaptureResolutionToIndex(settings.captureResolution);
            if (ImGui::Combo(
                    "Capture Resolution##LightProbeVolume",
                    &resolutionIndex,
                    resolutionLabels,
                    3)) {
                settings.captureResolution = CaptureResolutionFromIndex(resolutionIndex);
                changed = true;
            }

            float intensity = settings.intensity;
            if (ImGui::DragFloat("Intensity##LightProbeVolume", &intensity, 0.01f, 0.0f, 4.0f)) {
                settings.intensity = intensity;
                changed = true;
                runtimeChanged = true;
            }

            if (changed) {
                ClampLightProbeVolumeSettings(settings);
                scene.SetUnsavedSceneChanges(true);
                if (runtimeChanged) {
                    scene.RefreshLightingRuntime();
                }
            }

            ImGui::Text("Probe Count: %u", GetLightProbeVolumeProbeCount(settings));
            ImGui::Text("Estimated Capture Faces: %u", GetLightProbeVolumeProbeCount(settings) * 6u);
            ImGui::TextWrapped("Volume Output: %s", volumePath.generic_string().c_str());
        }
#endif

        bool OpenFolderInShell(const std::filesystem::path& folder, std::string& outMessage) {
            std::error_code ec{};
            if (!std::filesystem::exists(folder, ec)) {
                outMessage = "Bake folder does not exist: " + folder.generic_string();
                return false;
            }

            if (!EDITOR::SHELL::OpenFolderInExplorer(folder)) {
                outMessage = "Failed to open bake folder: " + folder.generic_string();
                return false;
            }

            outMessage = "Opened bake folder: " + folder.generic_string();
            return true;
        }

    } // namespace

    void LightingBakePanel::Draw(DocumentSceneBase& scene, bool& open) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Lighting Bake", &open)) {
            ImGui::End();
            return;
        }

        const TOOLS::BAKING::LightingBakeRequest request = BuildLightingBakeRequest(scene);
        const std::filesystem::path bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(
            request.projectRoot,
            request.sceneGuid);
        const std::filesystem::path manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(
            request.projectRoot,
            request.sceneGuid);
        const std::filesystem::path lightProbeVolumePath = ASSETS::LIGHTING::BuildLightProbeVolumeOutputPath(
            request.projectRoot,
            request.sceneGuid);

        ImGui::TextUnformatted("Current Scene");
        ImGui::Separator();
        ImGui::Text("Scene Name: %s", request.sceneName.empty() ? "(unnamed)" : request.sceneName.c_str());
        ImGui::Text("Scene GUID: %s", request.sceneGuid.empty() ? "(transient)" : request.sceneGuid.c_str());
        ImGui::TextWrapped("Scene Path: %s", request.scenePath.empty() ? "(unsaved)" : request.scenePath.c_str());
        ImGui::TextWrapped("Bake Root: %s", bakeRoot.generic_string().c_str());
        ImGui::TextWrapped("Manifest Path: %s", manifestPath.generic_string().c_str());

        DrawLightProbeVolumeSettings(scene, lightProbeVolumePath);

        ImGui::Spacing();
        ImGui::TextUnformatted("Bake Actions");
        ImGui::Separator();

        // Route bake actions through the service layer to keep runtime coupling narrow.
        TOOLS::BAKING::LightingBakeService service{};
        const bool bakeBusy =
            TOOLS::BAKING::IsLightingBakeJobRunning(
                scene.GetLightingBakeJobState());
        if (ImGui::Button("Validate Lighting Bake Setup")) {
            lastReport_ = service.ValidateLightingBakeSetup(request);
            hasReport_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Prepare Lighting Bake Manifest")) {
            lastReport_ = service.PrepareLightingBakeManifest(request);
            hasReport_ = true;
            scene.RefreshLightingRuntime();
            RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshotIfChanged(
                "LightingBake.PrepareManifest",
                &scene.GetSceneEnvironment());
        }
        if (bakeBusy) {
            ImGui::TextDisabled("Bake job is running.");
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Bake Reflection Probes Only")) {
            lastReport_ = service.BakeReflectionProbesOnly(scene);
            hasReport_ = true;
            RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshotIfChanged(
                "LightingBake.ReflectionProbe",
                &scene.GetSceneEnvironment());
        }
        if (ImGui::Button("Bake Light Probes Only")) {
            lastReport_ = service.BakeLightProbesOnly(scene);
            hasReport_ = true;
            RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshotIfChanged(
                "LightingBake.LightProbe",
                &scene.GetSceneEnvironment());
        }
        if (ImGui::Button("Clear Lighting Bake")) {
            lastReport_ = service.ClearLightingBake(request);
            hasReport_ = true;
            scene.RefreshLightingRuntime();
            RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshotIfChanged(
                "LightingBake.Clear",
                &scene.GetSceneEnvironment());
        }
        if (bakeBusy) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Bake Folder")) {
            if (OpenFolderInShell(bakeRoot, lastOpenFolderMessage_)) {
                HIKARI_LOG_INFO("[LightingBake] " + lastOpenFolderMessage_);
            } else {
                HIKARI_LOG_WARN("[LightingBake] " + lastOpenFolderMessage_);
            }
        }
        if (!lastOpenFolderMessage_.empty()) {
            ImGui::TextDisabled("%s", lastOpenFolderMessage_.c_str());
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Future Bake Targets");
        ImGui::Separator();
        ImGui::BulletText("Reflection Probe Baker: Basic Single Probe");
        ImGui::BulletText("Light Probe Baker: SH Volume Grid");
        ImGui::BulletText("Lightmap Baker: Not Implemented");

        ImGui::Spacing();
        ImGui::TextUnformatted("Last Report");
        ImGui::Separator();
        if (scene.HasLastLightingBakeReport()) {
            lastReport_ = scene.GetLastLightingBakeReport();
            hasReport_ = true;
        }
        if (!hasReport_) {
            ImGui::TextDisabled("No bake action has been run.");
        } else {
            ImGui::Text("Action: %s", TOOLS::BAKING::ToString(lastReport_.action));
            ImGui::Text("Target: %s", TOOLS::BAKING::ToString(lastReport_.target));
            ImGui::Text("Job State: %s", TOOLS::BAKING::ToString(lastReport_.jobState));
            ImGui::Text("Result: %s", lastReport_.success ? "Success" : "Failed");
            ImGui::Text("Warnings: %zu", lastReport_.warnings.size());
            ImGui::Text("Errors: %zu", lastReport_.errors.size());
            ImGui::Text("Manifest Written: %s", lastReport_.manifestWritten ? "Yes" : "No");
            ImGui::Text("Folder Created: %s", lastReport_.bakeFolderCreated ? "Yes" : "No");
            ImGui::Text("Folder Cleared: %s", lastReport_.bakeFolderCleared ? "Yes" : "No");
            ImGui::Text("Probe Capture Written: %s", lastReport_.reflectionProbeCaptured ? "Yes" : "No");
            ImGui::Text("Probe Scene Captured: %s", lastReport_.reflectionProbeSceneCaptured ? "Yes" : "No");
            ImGui::Text("Source Override Used: %s", lastReport_.reflectionProbeUsedSourceOverride ? "Yes" : "No");
            ImGui::Text("Probe Prefiltered: %s", lastReport_.reflectionProbePrefiltered ? "Yes" : "No");
            ImGui::Text("Probe Record Written: %s", lastReport_.reflectionProbeRecordWritten ? "Yes" : "No");
            ImGui::Text("Capture Validated: %s", lastReport_.reflectionProbeCaptureValidated ? "Yes" : "No");
            ImGui::Text("Prefilter Validated: %s", lastReport_.reflectionProbePrefilterValidated ? "Yes" : "No");
            ImGui::Text("Light Probe Baked: %s", lastReport_.lightProbeBaked ? "Yes" : "No");
            ImGui::Text("Light Probe Volume Written: %s", lastReport_.lightProbeVolumeWritten ? "Yes" : "No");
            ImGui::Text("Light Probe Record Written: %s", lastReport_.lightProbeRecordWritten ? "Yes" : "No");
            ImGui::Text("Light Probe Runtime Loaded: %s", lastReport_.lightProbeRuntimeLoaded ? "Yes" : "No");
            if (lastReport_.reflectionProbeCapturedFaceCount > 0 ||
                lastReport_.reflectionProbeQueuedReadbackFaceCount > 0) {
                ImGui::Text(
                    "Captured Faces: %u / Queued Readback: %u",
                    lastReport_.reflectionProbeCapturedFaceCount,
                    lastReport_.reflectionProbeQueuedReadbackFaceCount);
            }
            if (lastReport_.lightProbeCapturedFaceCount > 0 ||
                lastReport_.lightProbeQueuedReadbackFaceCount > 0) {
                ImGui::Text(
                    "Light Probe Faces: %u / Queued Readback: %u",
                    lastReport_.lightProbeCapturedFaceCount,
                    lastReport_.lightProbeQueuedReadbackFaceCount);
                ImGui::Text(
                    "Light Probe Cursor: probe %u face %u",
                    lastReport_.lightProbeCurrentProbeIndex,
                    lastReport_.lightProbeCurrentFaceIndex);
            }
            if (lastReport_.reflectionProbeCaptureResolution > 0) {
                ImGui::Text("Capture Resolution: %u", lastReport_.reflectionProbeCaptureResolution);
            }
            if (lastReport_.lightProbeCaptureResolution > 0) {
                ImGui::Text("Light Probe Capture Resolution: %u", lastReport_.lightProbeCaptureResolution);
            }
            if (lastReport_.lightProbeProbeCount > 0) {
                ImGui::Text("Light Probe Count: %u", lastReport_.lightProbeProbeCount);
            }
            if (!lastReport_.reflectionProbeCaptureFormat.empty()) {
                ImGui::Text("Capture Format: %s", lastReport_.reflectionProbeCaptureFormat.c_str());
            }
            if (lastReport_.reflectionProbeCaptureMipCount > 0) {
                ImGui::Text("Capture Mips: %u", lastReport_.reflectionProbeCaptureMipCount);
            }
            if (lastReport_.reflectionProbePrefilteredMipCount > 0) {
                ImGui::Text("Prefiltered Mips: %u", lastReport_.reflectionProbePrefilteredMipCount);
            }
            if (!lastReport_.reflectionProbePrefilteredFormat.empty()) {
                ImGui::Text("Prefiltered Format: %s", lastReport_.reflectionProbePrefilteredFormat.c_str());
            }
            if (!lastReport_.reflectionProbeCaptureMode.empty()) {
                ImGui::Text("Capture Mode: %s", lastReport_.reflectionProbeCaptureMode.c_str());
            }
            if (lastReport_.gpuFenceValue != 0) {
                ImGui::Text("GPU Fence: %llu", static_cast<unsigned long long>(lastReport_.gpuFenceValue));
            }
            ImGui::Text("Reflection Probe Records: %u", lastReport_.reflectionProbeRecordCount);
            ImGui::Text("Light Probe Records: %u", lastReport_.lightProbeRecordCount);
            ImGui::Text("Lightmap Records: %u", lastReport_.lightmapRecordCount);
            if (!lastReport_.reflectionProbeCapturePath.empty()) {
                ImGui::TextWrapped("Probe Capture: %s", lastReport_.reflectionProbeCapturePath.generic_string().c_str());
            }
            if (!lastReport_.reflectionProbePrefilteredPath.empty()) {
                ImGui::TextWrapped("Probe Prefiltered: %s", lastReport_.reflectionProbePrefilteredPath.generic_string().c_str());
            }
            if (!lastReport_.reflectionProbeBrdfLutPath.empty()) {
                ImGui::TextWrapped("Probe BRDF LUT: %s", lastReport_.reflectionProbeBrdfLutPath.generic_string().c_str());
            }
            if (!lastReport_.lightProbeVolumePath.empty()) {
                ImGui::TextWrapped("Light Probe Volume: %s", lastReport_.lightProbeVolumePath.generic_string().c_str());
            }
            if (!lastReport_.lightProbeDebugJsonPath.empty()) {
                ImGui::TextWrapped("Light Probe Debug JSON: %s", lastReport_.lightProbeDebugJsonPath.generic_string().c_str());
            }

            const std::string& lastMessage = LastText(lastReport_.messages);
            if (!lastMessage.empty()) {
                ImGui::TextWrapped("Last Message: %s", lastMessage.c_str());
            }

            DrawReportLines("Light Probe Messages", lastReport_.lightProbeMessages, ImVec4(0.65f, 0.84f, 1.0f, 1.0f));
            DrawReportLines("Warnings", lastReport_.warnings, ImVec4(1.0f, 0.82f, 0.35f, 1.0f));
            DrawReportLines("Errors", lastReport_.errors, ImVec4(1.0f, 0.42f, 0.35f, 1.0f));
        }

        ImGui::End();
#else
        (void)scene;
        (void)open;
#endif
    }

} // namespace HIKARI
