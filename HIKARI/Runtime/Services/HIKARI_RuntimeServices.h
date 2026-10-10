#pragma once

#include "Input/Runtime/HIKARI_InputService.h"
#include "Runtime/HIKARI_RuntimeHost.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"
#include "Runtime/Services/HIKARI_ActionTimeDilationService.h"
#include "Runtime/Services/HIKARI_GameProjectSaveStorageService.h"

#include <string>
#include <vector>

namespace HIKARI::SERVICES {
RuntimeHostMode GetRuntimeHostMode();
INPUT::InputService& GetInputService();
const INPUT::InputSnapshot& GetInputSnapshot();
ActionTimeDilationService& GetActionTimeDilationService();
GameProjectSaveStorageService& GetGameProjectSaveStorageService();

bool IsEditorHost();
bool IsStandaloneGameHost();
bool IsEditorUIEnabled();
bool ShouldProduceEditorUiFrame();
int GetLogicalScreenWidth();
int GetLogicalScreenHeight();

void RequestRuntimeSessionEnd();
bool IsRuntimeSessionEndRequested();
bool ConsumeRuntimeSessionEndRequest();

const std::string& GetRuntimeStartupSceneGuid();
// 開始シーンの内容を読む文件（空なら資産の文件）。
const std::string& GetRuntimeStartupSceneSnapshot();
bool IsRuntimeSceneGuidAllowed(const std::string& sceneGuid);

void SetEditorUIEnabled(bool enabled);

bool Initialize(const char* title, const RUNTIME::HostBootstrapConfig& cfg = {}, std::string* error = nullptr,
                bool startHidden = false);
void FinalizeAll();
bool PumpMessages();
} // namespace HIKARI::SERVICES
