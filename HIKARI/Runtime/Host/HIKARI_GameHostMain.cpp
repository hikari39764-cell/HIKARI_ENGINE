#include <Windows.h>

#include "Runtime/App/HIKARI_RuntimeEngineApp.h"
#include "Runtime/Host/HIKARI_EngineHostRunner.h"
#include "Runtime/Project/HIKARI_IGameProjectModule.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    std::unique_ptr<HIKARI::RUNTIME::IGameProjectModule> gameProject =
        HIKARI::RUNTIME::CreateGameProjectModule();
    if (!gameProject) {
        return -3;
    }
    HIKARI::RUNTIME::RuntimeEngineApp application{ *gameProject };
    HIKARI::RUNTIME::EngineHostRunOptions options{};
    options.product =
        HIKARI::RUNTIME::EngineHostProduct::Game;
    return HIKARI::RUNTIME::RunEngineHost(
        application,
        options);
}
