#pragma once

#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"
#include "Gfx/HIKARI_Dx12Core.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Platform/HIKARI_Win32Window.h"
#include "Runtime/HIKARI_GamePresentationController.h"
#include "Runtime/HIKARI_RuntimeHost.h"
#include "Runtime/Services/HIKARI_ActionTimeDilationService.h"
#include "Runtime/Services/HIKARI_GameProjectSaveStorageService.h"

#include <cstdint>
#include <string>
#include <vector>

namespace HIKARI::SERVICES::INTERNAL {
    struct RuntimeServiceState {
        PLATFORM::Win32Window window{};
        INPUT::InputService inputService{};
        ActionTimeDilationService actionTimeDilationService{};
        GameProjectSaveStorageService gameProjectSaveStorage{};
        RUNTIME::GamePresentationController gamePresentationController{};
        GFX::Dx12Core core{};
        bool comInitialized = false;
        RuntimeHostMode runtimeHostMode = RuntimeHostMode::Game;
        bool enableImGui = false;
        bool enableEditorUI = false;
        bool runtimeSessionEndRequested = false;
        std::string runtimeStartupSceneGuid{};
        std::vector<std::string> runtimeExportedSceneGuids{};
        bool gpuFrameReady = false;
        bool presentationPrepared = false;
        bool sceneLayersRendered = false;
        bool hasPendingWindowResize = false;
        int pendingWindowWidth = 0;
        int pendingWindowHeight = 0;
        int logicalScreenWidth = PRESENTATION::kDefaultLogicalWidth;
        int logicalScreenHeight = PRESENTATION::kDefaultLogicalHeight;
        bool editorGameViewportVisible = false;
        int editorGameViewportWidth = 0;
        int editorGameViewportHeight = 0;
        std::wstring runtimeWindowBaseTitle{};
        double previewTelemetrySeconds = 0.0;
        uint64_t previewTelemetryRenderFrames = 0;
        uint64_t previewTelemetryPresentedFrames = 0;
    };

    RuntimeServiceState& GetRuntimeServiceState();
} // namespace HIKARI::SERVICES::INTERNAL
