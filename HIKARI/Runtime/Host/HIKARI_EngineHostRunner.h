#pragma once

namespace HIKARI::RUNTIME {

    class IHostApplication;

    enum class EngineHostProduct {
        Game,
        Editor,
    };

    enum class EngineHostProjectPolicy {
        Required,
        None,
    };

    struct EngineHostRunOptions {
        EngineHostProduct product = EngineHostProduct::Game;
        EngineHostProjectPolicy projectPolicy =
            EngineHostProjectPolicy::Required;
        bool loadRuntimeConfig = true;
        const char* windowTitle = "HIKARI_Ver1.3";
    };

    int RunEngineHost(
        IHostApplication& application,
        const EngineHostRunOptions& options = {});

} // namespace HIKARI::RUNTIME
