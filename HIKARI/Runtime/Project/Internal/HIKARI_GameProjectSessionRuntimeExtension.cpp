#include "Runtime/Project/Internal/HIKARI_GameProjectSessionRuntimeExtension.h"

#include <exception>
#include <string>

#include "Core/HIKARI_Logger.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::RUNTIME {

bool GameProjectSessionRuntimeExtension::RegisterWorldServices(WorldServiceRegistry& services) {
    return services.Register(*this);
}

IGameProjectSession* GameProjectSessionRuntimeExtension::GetGameSession() const noexcept {
    return gameStarted_ ? module_.GetGameSession() : nullptr;
}

bool GameProjectSessionRuntimeExtension::StartGameSession(World& world, std::string& outMessage) {
    outMessage.clear();
    if (gameStarted_) {
        outMessage = "GameProject session is already active.";
        return false;
    }
    if (!module_.CreateGameSession(outMessage)) {
        return false;
    }

    IGameProjectSession* session = module_.GetGameSession();
    if (session == nullptr) {
        gameStarted_ = true;
        sceneEntered_ = false;
        outMessage.clear();
        return true;
    }

    gameStarted_ = true;
    try {
        WorldView view = GameProjectSdkAccess::MakeWorldView(world);
        session->OnGameStarted(view);
    } catch (const std::exception& error) {
        outMessage = std::string("GameProject OnGameStarted failed: ") + error.what();
    } catch (...) {
        outMessage = "GameProject OnGameStarted failed with an unknown exception.";
    }
    if (!outMessage.empty()) {
        HIKARI_LOG_ERROR("[GameProjectSession] " + outMessage);
        return false;
    }

    outMessage.clear();
    return true;
}

bool GameProjectSessionRuntimeExtension::EnterScene(World& world, std::string& outMessage) noexcept {
    outMessage.clear();
    IGameProjectSession* session = module_.GetGameSession();
    if (!gameStarted_ || sceneEntered_) {
        return false;
    }

    sceneEntered_ = true;
    if (session == nullptr) {
        return true;
    }
    try {
        WorldView view = GameProjectSdkAccess::MakeWorldView(world);
        session->OnSceneEntered(view);
    } catch (const std::exception& error) {
        outMessage = std::string("GameProject OnSceneEntered failed: ") + error.what();
    } catch (...) {
        outMessage = "GameProject OnSceneEntered failed with an unknown exception.";
    }
    if (outMessage.empty()) {
        return true;
    }

    ExitScene(world);
    HIKARI_LOG_ERROR("[GameProjectSession] " + outMessage);
    return false;
}

void GameProjectSessionRuntimeExtension::ExitScene(World& world) noexcept {
    IGameProjectSession* session = module_.GetGameSession();
    if (!gameStarted_ || !sceneEntered_) {
        return;
    }

    sceneEntered_ = false;
    if (session == nullptr) {
        return;
    }
    try {
        WorldView view = GameProjectSdkAccess::MakeWorldView(world);
        session->OnSceneExiting(view);
    } catch (const std::exception& error) {
        HIKARI_LOG_ERROR(std::string("[GameProjectSession] OnSceneExiting failed: ") + error.what());
    } catch (...) {
        HIKARI_LOG_ERROR("[GameProjectSession] OnSceneExiting failed with an unknown exception.");
    }
}

void GameProjectSessionRuntimeExtension::StopGameSession() noexcept {
    IGameProjectSession* session = module_.GetGameSession();
    if (session != nullptr && gameStarted_) {
        try {
            session->OnGameStopped();
        } catch (const std::exception& error) {
            HIKARI_LOG_ERROR(std::string("[GameProjectSession] OnGameStopped failed: ") + error.what());
        } catch (...) {
            HIKARI_LOG_ERROR("[GameProjectSession] OnGameStopped failed with an unknown exception.");
        }
    }
    if (gameStarted_) {
        module_.DestroyGameSession();
    }
    gameStarted_ = false;
    sceneEntered_ = false;
}

} // namespace HIKARI::RUNTIME
