#pragma once

namespace HIKARI::RUNTIME {

    class IHostApplication;

    enum class EngineHostProduct {
        Game,
        Editor,
    };

    struct EngineHostRunOptions {
        EngineHostProduct product = EngineHostProduct::Game;
        const char* windowTitle = "HIKARI_Ver1.3";
    };

    int RunEngineHost(
        IHostApplication& application,
        const EngineHostRunOptions& options = {});

} // namespace HIKARI::RUNTIME
