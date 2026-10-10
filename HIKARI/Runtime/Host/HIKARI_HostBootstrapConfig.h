#pragma once

#include <string>
#include <vector>

#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"
#include "Runtime/HIKARI_RuntimeHost.h"

namespace HIKARI::RUNTIME {

struct HostBootstrapConfig {
    RuntimeHostMode hostMode = RuntimeHostMode::Game;
    bool enableDebugCamera = false;
    bool enableDebugLayer = true;
    bool resizableWindow = true;
    bool enableImGui = false;
    bool enableEditorUI = false;
    bool enableStreamline = true;
    // 明示的な有界キャプチャ専用。ウィンドウ公開・音声・入力取得を行わない。
    bool headlessRender = false;
    int windowWidth = PRESENTATION::kDefaultLogicalWidth;
    int windowHeight = PRESENTATION::kDefaultLogicalHeight;
    std::string startupSceneGuid{};
    // startupSceneGuid の内容を読む文件（保存前の場面の写し）。空なら資産の文件。
    std::string startupSceneSnapshot{};
    std::vector<std::string> exportedSceneGuids{};
};

} // namespace HIKARI::RUNTIME
