#include "HIKARI_RuntimeLaunchConfig.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <json.hpp>

#include "HIKARI_Services.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Render3D/Settings/HIKARI_RenderQualityProfileStore.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"

namespace HIKARI {

    namespace {

        std::filesystem::path ExeDirectory() {
            std::array<wchar_t, MAX_PATH> buffer{};
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size()) {
                return std::filesystem::current_path();
            }
            return std::filesystem::path(buffer.data()).parent_path();
        }

        const nlohmann::json& RuntimeNode(const nlohmann::json& root) {
            if (root.contains("runtime") && root["runtime"].is_object()) {
                return root["runtime"];
            }
            return root;
        }

        std::optional<bool> ReadBool(const nlohmann::json& node, const char* key) {
            if (!node.contains(key) || !node[key].is_boolean()) {
                return std::nullopt;
            }
            return node[key].get<bool>();
        }

        std::optional<int> ReadInt(const nlohmann::json& node, const char* key) {
            if (!node.contains(key) || !node[key].is_number_integer()) {
                return std::nullopt;
            }
            return node[key].get<int>();
        }

        std::optional<std::string> ReadString(const nlohmann::json& node, const char* key) {
            if (!node.contains(key) || !node[key].is_string()) {
                return std::nullopt;
            }
            return node[key].get<std::string>();
        }

        std::optional<std::vector<std::string>> ReadStringArray(const nlohmann::json& node, const char* key) {
            if (!node.contains(key) || !node[key].is_array()) {
                return std::nullopt;
            }

            std::vector<std::string> values{};
            for (const nlohmann::json& entry : node[key]) {
                if (!entry.is_string()) {
                    continue;
                }
                std::string value = entry.get<std::string>();
                if (!value.empty()) {
                    values.push_back(std::move(value));
                }
            }
            return values;
        }

        void AddCandidate(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& root) {
            if (root.empty()) {
                return;
            }
            candidates.push_back(root / "HIKARI" / "runtime_config.json");
            candidates.push_back(root / "runtime_config.json");
        }

        std::filesystem::path ResolveConfigProjectRoot(const std::filesystem::path& path) {
            std::error_code ec{};
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (ec || normalized.empty()) {
                ec.clear();
                normalized = std::filesystem::absolute(path, ec);
            }
            if (ec || normalized.empty()) {
                normalized = path;
            }

            std::filesystem::path configDirectory = normalized.lexically_normal().parent_path();
            if (configDirectory.filename() == "HIKARI") {
                configDirectory = configDirectory.parent_path();
            }
            return configDirectory.lexically_normal();
        }

        std::filesystem::path ResolveConfiguredProjectRoot(
            const std::filesystem::path& configPath,
            const std::string& configuredRoot) {
            std::filesystem::path root = configuredRoot;
            if (root.is_relative()) {
                root = configPath.parent_path() / root;
            }

            std::error_code ec{};
            std::filesystem::path normalized =
                std::filesystem::weakly_canonical(root, ec);
            if (ec || normalized.empty()) {
                ec.clear();
                normalized = std::filesystem::absolute(root, ec);
            }
            return ec || normalized.empty()
                ? root.lexically_normal()
                : normalized.lexically_normal();
        }

        std::optional<std::filesystem::path> ExplicitConfigPath() {
            std::array<wchar_t, 32768> buffer{};
            const DWORD length = GetEnvironmentVariableW(
                L"HIKARI_RUNTIME_CONFIG",
                buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size()) {
                return std::nullopt;
            }
            return std::filesystem::path(buffer.data());
        }

        std::optional<std::filesystem::path> CommandLineConfigPath() {
            int argumentCount = 0;
            LPWSTR* arguments = CommandLineToArgvW(
                GetCommandLineW(),
                &argumentCount);
            if (arguments == nullptr) {
                return std::nullopt;
            }

            std::optional<std::filesystem::path> path{};
            for (int index = 1; index + 1 < argumentCount; ++index) {
                if (std::wstring_view(arguments[index]) == L"--runtime-config") {
                    path = std::filesystem::path(arguments[index + 1]);
                    break;
                }
            }
            LocalFree(arguments);
            return path;
        }
    }

    RuntimeLaunchConfig LoadRuntimeLaunchConfigFromFile(const std::filesystem::path& path) {
        RuntimeLaunchConfig cfg{};
        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(path, root) ||
            !root.is_object()) {
            return cfg;
        }

        const nlohmann::json& runtime = RuntimeNode(root);
        if (!runtime.is_object()) {
            return cfg;
        }

        if (runtime.contains("hostMode") && runtime["hostMode"].is_string()) {
            RuntimeHostMode mode{};
            if (TryParseRuntimeHostMode(runtime["hostMode"].get<std::string>(), mode)) {
                cfg.hostMode = mode;
            }
        }

        cfg.enableImGui = ReadBool(runtime, "enableImGui");
        cfg.enableEditorUI = ReadBool(runtime, "enableEditorUI");
        cfg.enablePortableObjectTools = ReadBool(runtime, "enablePortableObjectTools");
        cfg.enableDebugLayer = ReadBool(runtime, "enableDebugLayer");
        cfg.enableDebugCamera = ReadBool(runtime, "enableDebugCamera");
        cfg.resizableWindow = ReadBool(runtime, "resizableWindow");
        cfg.windowWidth = ReadInt(runtime, "windowWidth");
        cfg.windowHeight = ReadInt(runtime, "windowHeight");
        if (runtime.contains("renderQuality") &&
            runtime["renderQuality"].is_object()) {
            RENDER3D::RenderQualitySettings renderQuality{};
            std::string renderQualityError{};
            if (RENDER3D::DeserializeRenderQualitySettings(
                    runtime["renderQuality"],
                    renderQuality,
                    &renderQualityError)) {
                cfg.renderQuality = renderQuality;
            }
            else {
                HIKARI_LOG_WARN(
                    "[RuntimeConfig] " + renderQualityError +
                    " Path: " + path.string());
            }
        }
        cfg.antiAliasingMode = ReadString(runtime, "antiAliasingMode");
        cfg.dlssQualityMode = ReadString(runtime, "dlssQualityMode");
        cfg.frameGenerationMode = ReadString(runtime, "frameGenerationMode");
        cfg.frameGenerationMultiplier =
            ReadInt(runtime, "frameGenerationMultiplier");
        cfg.startupSceneGuid = ReadString(runtime, "startupSceneGuid");
        cfg.exportedSceneGuids = ReadStringArray(runtime, "exportedSceneGuids");
        cfg.loaded = true;
        cfg.sourcePath = path;
        cfg.projectRoot = ResolveConfigProjectRoot(path);
        if (const std::optional<std::string> projectRoot =
                ReadString(runtime, "projectRoot")) {
            cfg.projectRoot = ResolveConfiguredProjectRoot(path, *projectRoot);
        }
        return cfg;
    }

    RuntimeLaunchConfig LoadRuntimeLaunchConfig() {
        if (const std::optional<std::filesystem::path> commandLinePath =
                CommandLineConfigPath()) {
            RuntimeLaunchConfig cfg = LoadRuntimeLaunchConfigFromFile(*commandLinePath);
            if (cfg.loaded) {
                return cfg;
            }
        }
        if (const std::optional<std::filesystem::path> explicitPath =
                ExplicitConfigPath()) {
            RuntimeLaunchConfig cfg = LoadRuntimeLaunchConfigFromFile(*explicitPath);
            if (cfg.loaded) {
                return cfg;
            }
        }

        std::vector<std::filesystem::path> candidates{};
        AddCandidate(candidates, std::filesystem::current_path());
        AddCandidate(candidates, ExeDirectory());

        std::error_code ec{};
        for (const std::filesystem::path& candidate : candidates) {
            if (std::filesystem::exists(candidate, ec) && !ec) {
                RuntimeLaunchConfig cfg = LoadRuntimeLaunchConfigFromFile(candidate);
                if (cfg.loaded) {
                    return cfg;
                }
            }
            ec.clear();
        }
        return {};
    }

    bool ApplyRuntimeWorkingDirectory(const RuntimeLaunchConfig& runtimeCfg) {
        if (!runtimeCfg.loaded || runtimeCfg.projectRoot.empty()) {
            return false;
        }

        std::error_code ec{};
        std::filesystem::current_path(runtimeCfg.projectRoot, ec);
        return !ec;
    }

    void ApplyRuntimeLaunchConfig(const RuntimeLaunchConfig& runtimeCfg, SERVICES::BootstrapConfig& servicesCfg) {
        if (runtimeCfg.hostMode) {
            servicesCfg.hostMode = *runtimeCfg.hostMode;
        }
        if (runtimeCfg.enableImGui) {
            servicesCfg.enableImGui = *runtimeCfg.enableImGui;
        }
        if (runtimeCfg.enableEditorUI) {
            servicesCfg.enableEditorUI = *runtimeCfg.enableEditorUI;
        }
        if (runtimeCfg.enablePortableObjectTools) {
            servicesCfg.enablePortableObjectTools = *runtimeCfg.enablePortableObjectTools;
        }
        if (runtimeCfg.enableDebugLayer) {
            servicesCfg.enableDebugLayer = *runtimeCfg.enableDebugLayer;
        }
        if (runtimeCfg.enableDebugCamera) {
            servicesCfg.enableDebugCamera = *runtimeCfg.enableDebugCamera;
        }
        if (runtimeCfg.startupSceneGuid) {
            servicesCfg.startupSceneGuid = *runtimeCfg.startupSceneGuid;
        }
        if (runtimeCfg.exportedSceneGuids) {
            servicesCfg.exportedSceneGuids = *runtimeCfg.exportedSceneGuids;
        }

        RENDER3D::RenderQualitySettings quality{};
        std::error_code rootError{};
        const std::filesystem::path projectRoot = runtimeCfg.projectRoot.empty()
            ? std::filesystem::current_path(rootError)
            : runtimeCfg.projectRoot;
        std::string profileError{};
        const bool profileLoaded = !projectRoot.empty() &&
            RENDER3D::LoadRenderQualityProfile(
                projectRoot,
                quality,
                &profileError);
        if (!profileLoaded && !profileError.empty()) {
            HIKARI_LOG_WARN("[RenderQualityProfile] " + profileError);
        }

        if (runtimeCfg.renderQuality) {
            quality = *runtimeCfg.renderQuality;
        }
        if (runtimeCfg.antiAliasingMode) {
            (void)RENDER3D::TryParseRenderAntiAliasingMode(
                *runtimeCfg.antiAliasingMode,
                quality.antiAliasingMode);
        }
        if (runtimeCfg.dlssQualityMode) {
            (void)RENDER3D::TryParseDlssQualityMode(
                *runtimeCfg.dlssQualityMode,
                quality.dlssQualityMode);
        }
        if (runtimeCfg.frameGenerationMode) {
            (void)RENDER3D::TryParseRenderFrameGenerationMode(
                *runtimeCfg.frameGenerationMode,
                quality.frameGenerationMode);
        }
        if (runtimeCfg.frameGenerationMultiplier) {
            quality.frameGenerationMultiplier = static_cast<uint8_t>(
                std::clamp(*runtimeCfg.frameGenerationMultiplier, 2, 6));
        }
        RENDER3D::SetRenderQualitySettings(quality);

        if (!IsEditorHostMode(servicesCfg.hostMode)) {
            const RENDER3D::RenderQualitySettings& resolvedQuality =
                RENDER3D::GetRenderQualitySettings();
            const RENDER3D::RenderResolution windowResolution =
                RENDER3D::ResolveFixedRenderResolution(resolvedQuality.windowSize);
            servicesCfg.resizableWindow =
                resolvedQuality.windowMode ==
                RENDER3D::WindowPresentationMode::Windowed;
            if (windowResolution.width > 0) {
                servicesCfg.windowWidth = windowResolution.width;
            }
            if (windowResolution.height > 0) {
                servicesCfg.windowHeight = windowResolution.height;
            }
        }

        if (runtimeCfg.resizableWindow) {
            servicesCfg.resizableWindow = *runtimeCfg.resizableWindow;
        }
        if (runtimeCfg.windowWidth && *runtimeCfg.windowWidth > 0) {
            servicesCfg.windowWidth = *runtimeCfg.windowWidth;
        }
        if (runtimeCfg.windowHeight && *runtimeCfg.windowHeight > 0) {
            servicesCfg.windowHeight = *runtimeCfg.windowHeight;
        }
    }

} // namespace HIKARI
