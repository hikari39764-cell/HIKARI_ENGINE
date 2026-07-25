#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "HIKARI_RuntimeHost.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"

namespace HIKARI {

    struct RuntimeLaunchConfig {
        bool loaded = false;
        std::filesystem::path sourcePath{};
        std::filesystem::path projectRoot{};
        std::optional<std::filesystem::path> requestedProjectPath{};

        std::optional<RuntimeHostMode> hostMode{};
        std::optional<bool> enableImGui{};
        std::optional<bool> enableEditorUI{};
        std::optional<bool> enableDebugLayer{};
        std::optional<bool> enableDebugCamera{};
        std::optional<bool> resizableWindow{};
        std::optional<int> windowWidth{};
        std::optional<int> windowHeight{};
        std::optional<RENDER3D::RenderQualitySettings> renderQuality{};

        // Legacy flat launch overrides. New configs use renderQuality.
        std::optional<std::string> antiAliasingMode{};
        std::optional<std::string> dlssQualityMode{};
        std::optional<std::string> frameGenerationMode{};
        std::optional<int> frameGenerationMultiplier{};
        std::optional<std::string> startupSceneGuid{};
        std::optional<std::vector<std::string>> exportedSceneGuids{};
    };

    RuntimeLaunchConfig LoadRuntimeLaunchConfig();
    RuntimeLaunchConfig LoadRuntimeLaunchConfigFromFile(const std::filesystem::path& path);
    void ApplyRuntimeLaunchConfig(
        const RuntimeLaunchConfig& runtimeCfg,
        RUNTIME::HostBootstrapConfig& servicesCfg);

} // namespace HIKARI
