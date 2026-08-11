#pragma once

#include "Input/Runtime/HIKARI_InputService.h"
#include "Runtime/HIKARI_RuntimeHost.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"

#include <string>
#include <vector>

namespace HIKARI::SERVICES {
    RuntimeHostMode GetRuntimeHostMode();
    INPUT::InputService& GetInputService();
    const INPUT::InputSnapshot& GetInputSnapshot();

    bool IsEditorHost();
    bool IsStandaloneGameHost();
    bool IsImGuiEnabled();
    bool IsEditorUIEnabled();
    bool ShouldProduceEditorUiFrame();

    const std::string& GetRuntimeStartupSceneGuid();
    const std::vector<std::string>& GetRuntimeExportedSceneGuids();
    bool IsRuntimeSceneGuidAllowed(const std::string& sceneGuid);

    void SetEditorUIEnabled(bool enabled);

    bool Initialize(
        const char* title,
        const RUNTIME::HostBootstrapConfig& cfg = {});
    void FinalizeAll();
    bool PumpMessages();
} // namespace HIKARI::SERVICES
