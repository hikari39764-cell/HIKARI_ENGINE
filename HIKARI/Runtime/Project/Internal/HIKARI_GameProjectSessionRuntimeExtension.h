#pragma once

#include <string>
#include <string_view>

#include "HIKARI/Project/GameProjectSession.h"
#include "Runtime/Project/HIKARI_GameProjectModuleLibrary.h"
#include "Scene/Features/HIKARI_RuntimeExtension.h"

namespace HIKARI {

class World;
class WorldView;

namespace RUNTIME {

class GameProjectSessionRuntimeExtension final : public IRuntimeExtension {
public:
    explicit GameProjectSessionRuntimeExtension(GameProjectModuleLibrary& module) noexcept : module_(module) {}

    std::string_view GetExtensionId() const noexcept override { return "HIKARI.GameProjectSession"; }
    bool RegisterWorldServices(WorldServiceRegistry& services) override;
    bool RegisterRuntimeFeatures(RuntimeFeatureCatalog&) override { return true; }

    IGameProjectSession* GetGameSession() const noexcept;
    bool StartGameSession(World& world, std::string& outMessage);
    bool EnterScene(World& world, std::string& outMessage) noexcept;
    void ExitScene(World& world) noexcept;
    void StopGameSession() noexcept;

private:
    GameProjectModuleLibrary& module_;
    bool gameStarted_ = false;
    bool sceneEntered_ = false;
};

} // namespace RUNTIME
} // namespace HIKARI
