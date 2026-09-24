#include <Windows.h>

#include "Runtime/App/HIKARI_RuntimeEngineApp.h"
#include "Runtime/Host/HIKARI_EngineHostRunner.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // ランタイムエンジンホストを起動
    HIKARI::RUNTIME::RuntimeEngineApp application{};
    HIKARI::RUNTIME::EngineHostRunOptions options{};
    options.product = HIKARI::RUNTIME::EngineHostProduct::Game;
    return HIKARI::RUNTIME::RunEngineHost(application, options);
}
